/**********************************************************************

  Utilities.ino - LX200 protocol routines
  Copyright ©2017-2025 AdNovea®

  This original work is provided ASIS.

***********************************************************************/




#define   SHORT_PRESS_2S          20                            // ~1s   ( 10 × 100 ms)
#define   SHORT_PRESS_4S          40                            // ~3s   ( 30 × 100 ms)
#define   SHORT_PRESS_6S          60                            // ~5s   ( 50 × 100 ms)
#define   SERIAL_INPUT_MAX_LEN    50                            // Buffer for incoming data (longest is scheduler cmd)

const char _HELP[] PROGMEM = "\n COMMANDS:\n"
                             "\t?\t\tShow this help\n"
                             "\n"
                             "\tPARK  \t\tPark telescope tube to HOME position\n"
                             "\tHOME  \t\tSet current position as HOME and get Autostar info\n"
                             //                             "\tMODEL=\t\tSelect telescope from the list\n"
                             "\tMOUNT=\t\tSet Polar=0 or AltAz=1\n"
                             "\n"
                             "\tCOMMS=\t\tSet AUX=0 or BLT=1\n"
                             "\tPARAMS\t\tPrint parameters\n"
                             "\tLOG   \t\tPrint LX200 debug messages\n"
                             "\tSAVE  \t\tSave parameters\n"
                             "\tBOOT  \t\tReboot module\n"
                             "\n"
                             "  LX200 commands to configure ETX Link and Autostar:\n"
                             "  #:SGsHH.H#        Set Time zone*  (Paris #:SG-02.0# to GMT) \n"
                             "  #:SCMM/DD/YY#     Set Date        (e.g.  #:SC09/25/26#)\n"
                             "  #:SLHH:MM:SS#     Set Local Time  (e.g.  #:SL17:35:00#)\n"
                             "  #:StsDD*MM#       Set Latitude    (Paris #:St+48*51# +North, -South)\n"
                             "  #:SgsDDD*MM#      Set Longitude   (Paris #:Sg002*20# [0-360] CW)\n"
                             "  * LX200 protocol need inverted sign.\n"
                             "\n";
const char _DONE[] PROGMEM = "Done";


/* ************************************************************
   SYS: Functions
 ************************************************************ */

// ------------------------------------------------------------
// SYS: Read button state
// ------------------------------------------------------------
void readButtons()
{
  if ( digitalRead( PIN_BUT1 ) ) return;                      // Button not pressed, exit function

  int  iPress = 0;
  byte action = 0;
  while ( !digitalRead( PIN_BUT1 ) ) {                        // Loop while the button is kept pressed (LOW state)
    delay( 100 );
    iPress++;

    // Provide real-time feedback when reaching the thresholds
    if ( iPress == SHORT_PRESS_2S ) {                         // 2-second mark (between 1s and less than 5s)
      beep( _BEEP ); action++;
    }
    else if ( iPress == SHORT_PRESS_4S ) {                    // 4-second mark ( between 5s and less than 10s )
      beep( _BEEP2 ); action++;
    }
    else if ( iPress == SHORT_PRESS_6S ) {                    // 6-second
      beep( _BEEP3 ); action++;

    }
  }
  if      ( action == 1 ) setHome();                          // Set telescope to HOME, alignment to North & Horizon
  else if ( action == 2 ) setPark();                          // Set telescope to PARK position (vertical)
  else if ( action == 3 ) ESP.restart();                      // Restart ESP32
}


// ------------------------------------------------------------
// SYS: Log message to console
// ------------------------------------------------------------

void log_message( const __FlashStringHelper* msg, bool show ) {
  if ( show ) serialDBG.println( msg );
}
void log_message( const char* msg, bool show ) {
  if ( show ) serialDBG.println( msg );
}
void log_message( const String& msg, bool show ) {
  if ( show ) serialDBG.println( msg );
}


// ------------------------------------------------------------
// SYS: Display the version and compilation date-time
// ------------------------------------------------------------
void showVer()
{
  sprintf_P( log_msg, PSTR( "\n%s\n# %s v%s - %s %s\n%s"), _BAR, APP_NAME, VERSION, __DATE__, __TIME__, _BAR  );
  log_message( log_msg, true );
}


// ------------------------------------------------------------
// SYS: Buzzer frequency(Hz), duration (ms)
// ------------------------------------------------------------
void beep( byte mode )
{
  switch ( mode ) {
    case _READY :       buzzer( 440, 100 ); delay( 50 ); buzzer( 800, 100 ); break;
    case _OK :          buzzer( 800, 100 ); delay( 50 ); buzzer( 440, 100 ); break;
    case _WARNING:      buzzer( 400,  200 ); buzzer( 500 , 200 ); buzzer( 800, 200 ); break;
    case _FAIL:         buzzer( 100, 500 ); break;
    case _BEEP:         buzzer( 2000, 100 ); break;
    case _BEEP2:        buzzer( 2000, 100 ); delay( 50 ); buzzer( 2000, 100 ); break;
    case _BEEP3:        buzzer( 2000, 100 ); delay( 50 ); buzzer( 2000, 100 ); delay( 50 );  buzzer( 2000, 100 ); break;
  }
}
void buzzer( int freq, int len )
{
  ledcChangeFrequency( PIN_BUZZ, freq, 8 );                   // Set buzzer @ 2kHz
  ledcWrite( PIN_BUZZ, min( cfg.buzzer * 50, 250 ) );         // Activate buzzeer @ vol=[0-256]
  delay( len );
  ledcWrite( PIN_BUZZ, 0 );
}



/* ************************************************************
   CONSOLE: Functions
 ************************************************************ */

// ------------------------------------------------------------
//  SYS: Read Serial command line
// ------------------------------------------------------------
void readConsole()
{
  if ( !serialDBG.available() ) return;

  static char stBuff[SERIAL_INPUT_MAX_LEN];                   // Buffer for incoming data (longest is scheduler cmd)
  static int cmdIdx;                                          // Position in incoming data buffer

  while ( serialDBG.available() ) {
    char chCmd = serialDBG.read();

    if ( chCmd != '\n' && chCmd != '\r' ) {
      if ( cmdIdx < SERIAL_INPUT_MAX_LEN - 1 )                // Security: Prevent buffer overflow
        stBuff[cmdIdx++] = chCmd;
      else {
        cmdIdx = 0;
        stBuff[0] = '\0';
        while ( serialDBG.available() ) serialDBG.read();     // flush buffer
      }
    } else {
      if ( cmdIdx > 0 ) {                                     // Process only if buffer is not empty
        stBuff[cmdIdx] = '\0';                                // Terminate string safely
        while ( serialDBG.available() ) serialDBG.read();

        char *stParams = NULL;                                // Safe parsing without using hazardous strcpy on itself
        char *stCmd = strtok_r( stBuff, "=", &stParams );
        if ( stParams == NULL ) stParams = (char*)"";         // If no '=' was found, stParams would be NULL. Force it to an empty string ""
        if ( stCmd != NULL ) process_CMD( stCmd, stParams );  // Execute command
        cmdIdx = 0;                                           // Reset buffer pointer
      }
    }

    if ( cmdIdx < SERIAL_INPUT_MAX_LEN )
      stBuff[cmdIdx] = '\0';                                  // Always keep the buffer cleanly terminated
    delay(2);                                                 // Small delay for serial buffer stability
  }
}


// ------------------------------------------------------------
// SYS: Parse Serial command line
//      Arguments: command, parameters, ptr to output device (e.g. &Serial)
// ------------------------------------------------------------
bool process_CMD( char* stCmd, char* stParams )
{
  bool bok = true;

  // Send LX200 commands directly from console
  if ( stCmd[0] == '#' && strlen( stCmd ) > 2 ) {             // Cmd = #:x#
    //    writeETX( stCmd ); readETX( _GET ); and read reply
    int len = strlen( stCmd );
    if ( len > 0 && stCmd[len - 1] == '#' ) stCmd[len - 1] = '\0';
    parseLX200Command( stCmd + 1 ); return true;              // Send Cmd to LX200 parser
  }

  // Uppercase the command (stCmd)
  for ( int i = 0; i < strlen( stCmd ); i++ )    stCmd[i]    = toupper( stCmd[i] );
  for ( int i = 0; i < strlen( stParams ); i++ ) stParams[i] = toupper( stParams[i] );

  // Utility commmands
  if ( stCmd[0] == '?' ) serialDBG.println( (const __FlashStringHelper *)( _HELP ) );
  else if ( strcmp_P( stCmd, PSTR( "PARK"   ) ) == 0 ) setPark();                       // Park telescope's tube
  else if ( strcmp_P( stCmd, PSTR( "HOME"   ) ) == 0 ) setHome();                       // Set HOME and read telescope info
  else if ( strcmp_P( stCmd, PSTR( "MOUNT"  ) ) == 0 ) cfg.mount =  ( atoi( stParams ) == 1 ? 1 : 0 );

  else if ( strcmp_P( stCmd, PSTR( "SAVE"   ) ) == 0 ) ee_write( CONFIG_EEPROM_ADDR );  // Save settings
  else if ( strcmp_P( stCmd, PSTR( "BOOT"   ) ) == 0 ) ESP.restart();                   // Reboot module
  else if ( strcmp_P( stCmd, PSTR( "LOG"    ) ) == 0 ) _LOGLX = !_LOGLX;                // Toggle LX200 logs
  else if ( strcmp_P( stCmd, PSTR( "COMMS"  ) ) == 0 ) cfg.blt = atoi( stParams ) == 1; // connect to BLT or AUX
  /*
    else if ( strcmp_P( stCmd, PSTR( "MODEL"  ) ) == 0 ) {                                // Select telescope model
      if ( stParams[0] != '\0' && atoi( stParams ) >= 0 && atoi( stParams ) < 12 ) cfg.model = atoi( stParams );
      serialDBG.printf( "\n Telescopes with Autostar #494 controller\n%s\n", _BAR );
      for (int i = 0; i < 12; i++)  serialDBG.printf( " %2d %c\t%s (%s mm)\n", i, (i == cfg.model) ? '*' : ' ', list[i][1], list[i][2] );
      serialDBG.println( _BAR );
    }
  */
  else if ( strcmp_P( stCmd, PSTR( "PARAMS"  ) ) == 0 ) {                               // Select telescope model
    char loc_str[32], gmt_str[32];
    clocks( loc_str, gmt_str );                                                         // Get local & GMT clocks
    serialDBG.printf(
      "\n Parametres:\n%s\n"
      " Connected to  : %s\n\n"
      " Local time    : %s\n"
      " GMT time      : %s\n"
      " Time zone     : %+02d.0h\n\n"
      " Telescope     : %s\n"
      " Mount type    : %s\n"
      " Latitude      : %.2f°\n"
      " Longitude     : %.2f°\n"
      " Buzzer level  : %d\n"
      " LX200 logging : %d\n\n",
      _BAR, cfg.blt ? "BLT" : "AUX",
      loc_str, gmt_str, cfg.timezone,
      "Meade", cfg.mount == 0 ? "POLAR" : "ALTAZ",
      cfg.siteLat, cfg.siteLong,
      cfg.buzzer, _LOGLX );
  }

  // Invalid commands
  else bok = false;

  serialDBG.println( bok ? F( " Done" )  : F( " Unknown" ) );
  return bok;
}



/* ************************************************************
   EEPROM: Functions
 ************************************************************ */

// ------------------------------------------------------------
//  Read EEPROM
// ------------------------------------------------------------
void ee_read( int iOffset )
{
  // Check if EEPROM(0)=ID otherwise we use default values
  if ( EEPROM.read(0) == cfg.ID )
    for ( int i = 0; i < sizeof cfg; ++i ) {
      ( (byte*) &cfg )[i] = EEPROM.read( iOffset + i );
    }
  // EEPROM is empty or corrupted. Reload default values
  else {
    log_message( F( " EEPROM reset to default values" ), true );
    ee_write( iOffset );
  }
}


// ------------------------------------------------------------
//  Write EEPROM
// ------------------------------------------------------------
void ee_write( int iOffset )
{
  for ( int i = 0; i < sizeof cfg; ++i )
    EEPROM.write( iOffset + i, ( (byte*) &cfg)[i] );
  log_message( EEPROM.read(0) == cfg.ID && EEPROM.commit() ? F( " Settings Saved" ) : F( " Saving Failed!" ), true );
}



/* ************************************************************
   CLOCK: Functions
 ************************************************************ */

// ------------------------------------------------------------
// LX200: Set Internal Software Clock time from LX200 command (HH:MM:SS#)
// ------------------------------------------------------------
void setClockTime( char* stt )
{
  time_t now_ts;                                            // Read current time
  time( &now_ts );

  // 1. Reconstruct the current local epoch from the system GMT time
  time_t localEpoch = now_ts + (time_t)( cfg.timezone * 3600.0 );
  gmtime_r( &localEpoch, &locTime );                        // Populate locTime with current local date/time

  // 2. Overwrite only the local time fields from the LX200 string
  locTime.tm_hour  = ( stt[0] - '0' ) * 10 + ( stt[1] - '0' );
  locTime.tm_min   = ( stt[3] - '0' ) * 10 + ( stt[4] - '0' );
  locTime.tm_sec   = ( stt[6] - '0' ) * 10 + ( stt[7] - '0' );
  locTime.tm_isdst = 0;                                     // Disable DST to prevent mktime automatic adjustments

  // 3. Convert the updated local structure back into a local epoch
  localEpoch = mktime( &locTime );

  if ( localEpoch != -1 ) {
    // 4. Convert back to GMT epoch and commit to ESP32 registers
    time_t gmtEpoch = localEpoch - (time_t)( cfg.timezone * 3600.0 );

    struct timeval now;
    now.tv_sec  = gmtEpoch;
    now.tv_usec = 0;
    settimeofday( &now, NULL );

    // 5. Instantly refresh global gmtTime and locTime structures to maintain integrity
    utc_ts = gmtEpoch;                                      // UPDATE GLOBAL utc_ts
    gmtime_r( &gmtEpoch, &gmtTime );
    gmtime_r( &localEpoch, &locTime );
  }
}


// ------------------------------------------------------------
// LX200: Set Internal Software Clock date from LX200 command (MM/DD/YY#)
// ------------------------------------------------------------
void setClockDate( char* stt )
{
  int m = 0, d = 0, y = 0;

  // Use the local pointer argument "stt" shifted by 2 bytes to safely skip "SC"
  if ( sscanf( stt, "%d/%d/%d", &m, &d, &y ) == 3 ) {
    time_t now_ts;                                            // Read current time
    time( &now_ts );

    // 1. Reconstruct the current local epoch from the system GMT time
    time_t localEpoch = now_ts + (time_t)( cfg.timezone * 3600.0 );
    gmtime_r( &localEpoch, &locTime );                        // Populate locTime with current local date/time

    // 2. Overwrite only the local time fields from the LX200 string
    locTime.tm_mon   = m - 1;                                 // Current month 0:Jan, 11:Dec
    locTime.tm_mday  = d;                                     // Day of month (1-31)
    locTime.tm_year  = (2000 + y) - 1900;                     // Convert 2 chars in complete year and substract1900
    locTime.tm_isdst = 0;                                     // Disable DST to prevent mktime automatic adjustments

    // 3. Convert the updated local structure back into a local epoch
    localEpoch = mktime( &locTime );

    // 3. Convert the updated local structure back into a local epoch
    localEpoch = mktime( &locTime );

    if ( localEpoch != -1 ) {
      
      // 4. Convert back to GMT epoch and commit to ESP32 registers
      time_t gmtEpoch = localEpoch - (time_t)( cfg.timezone * 3600.0 );

      struct timeval now;
      now.tv_sec  = gmtEpoch;
      now.tv_usec = 0;
      settimeofday( &now, NULL );

      // 5. Instantly refresh global gmtTime and locTime structures to maintain integrity
      utc_ts = gmtEpoch;                                      // UPDATE GLOBAL utc_ts
      gmtime_r( &gmtEpoch, &gmtTime );
      gmtime_r( &localEpoch, &locTime );
    }
  }
}


// ------------------------------------------------------------
// LX200: Print Local & GMT clocks
// ------------------------------------------------------------
void clocks( char* stloc, char* stgmt )
{
  char loc_str[32];
  char gmt_str[32];

  // 0. Update the global unix timestamp from the ESP32 internal clock
  time( &utc_ts );
  gmtime_r( &utc_ts, &gmtTime );                              // Keep gmtTime perfectly synchronized

  // 1. Compute Local time from utc_ts
  time_t local_ts = utc_ts + (time_t)( cfg.timezone * 3600.0 );
  gmtime_r( &local_ts, &locTime );

  // 2. Format GMT, Timezone, LST, LMST, and location into log_msg buffer
  if ( _LOGS ) snprintf( stloc, sizeof(loc_str), "%02d/%02d/%02d %02d:%02d:%02d",
                           locTime.tm_mday, locTime.tm_mon + 1, (int)((locTime.tm_year + 1900) % 100),
                           locTime.tm_hour, locTime.tm_min, locTime.tm_sec );

  if ( _LOGS ) snprintf ( stgmt, sizeof(gmt_str), "%02d/%02d/%02d %02d:%02d:%02d",
                            gmtTime.tm_mday, gmtTime.tm_mon + 1, (int)((gmtTime.tm_year + 1900) % 100),
                            gmtTime.tm_hour, gmtTime.tm_min, gmtTime.tm_sec );
}


/* EOF */
