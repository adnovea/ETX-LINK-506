/**********************************************************************

  LX200_protocol.ino - LX200 protocol routines
  Copyright ©2017-2025 AdNovea®

  This original work is provided ASIS.

  Degree unit is ° 0xDF [223] -> replace by '*'

  Set Autostar:
  #:SGsHH.H#        Set Time zone*  (Paris #:SG-02.0#)
  #:SCMM/DD/YY#     Set Date        (e.g.  #:SC09/25/26#)
  #:SLHH:MM:SS#     Set Local Time  (e.g.  #:SL17:35:00#)
  #:StsDD*MM#       Set Latitude    (Paris #:St+46*51#)
  #:SgsDDD*MM#      Set Longitude*  (Paris #:Sg001*52#)
    Sign inverted with LX200 protocol.

  #:GR#             Read target Right Ascension
  #:GD#             Read target Declination
  #:SrHH:MM.T#      Set target Right Ascension [00:00,24:00[
  #:Sd±DD*MM#       Set target Declination     [+90:00,-90:00[
  #:MS#             GOTO target (Sr/Sd)
  #:CM#             SYNC telescope with target positon

  CRASH:
  #:CM#             is sensitive and may crash Autostar (we disable this feature)

***********************************************************************/



/* ************************************************************
   EXT: Functions
 ************************************************************ */

// ------------------------------------------------------------
// LX200: Read command from Application to parser
// ------------------------------------------------------------
void readAPP()
{
  if ( !serialAPP.available() ) return;                       // No data received

  while ( serialAPP.available() ) {
    if ( recvAPP.len > BUFMASK - 2 ) recvAPP.len = 0;         // Prevent buffer overflow
    char c = serialAPP.read();
    if ( c == 0xDF ) c = '*';                                 // Replace ° by * (Stellarium for PC use 0xDF)
    recvAPP.text[recvAPP.len++] = c;                          // Store character

    // TRIGGER: Standard LX200 protocol
    digitalWrite( LED_COMMS, HIGH );                          // COMMS LED on
    if ( c == 0x06 || c == '#' ) {                            // LX200: Check for '#' or 0x06 <ACK>
      if ( c == '#' ) {                                       // '#' at the end if present, or just close the string
        recvAPP.text[recvAPP.len - 1] = '\0';
        recvAPP.len--;                                        // Set actual length after character removed
      } else
        recvAPP.text[recvAPP.len] = '\0';
      parseLX200Command( recvAPP.text );                      // Route to LX200 parser eg. #:St+46*51#
      recvAPP.len = 0;                                        // Reset buffer length
    }
    digitalWrite( LED_COMMS, LOW );                           // COMMS LED off
  }
}


// ------------------------------------------------------------
// LX200: Send reply to Application
// ------------------------------------------------------------
void writeAPP( char* lxReply )
{
  if ( lxReply == NULL ) return;

  // Send response directly back to application
  serialAPP.write( (const uint8_t*)lxReply, strlen( lxReply ) );

  if ( _LOGLX ) {
    for ( char* p = lxReply; *p; p++ )
        if ( isprint(*p) ) serialDBG.print(*p); else serialDBG.printf("[%d]", (unsigned char)*p);
  }
}


// ------------------------------------------------------------
// LX200: Read reply from ETX ( _FLUSH, _THRU or _GET
// ------------------------------------------------------------
char* readETX( byte funct )
{
  int len    = 0;
  recvETX[0] = '\0';
  unsigned long etxTimeout = millis();

  // Wait for a response with 2sec timeout
  while ( !serialETX.available() ) {
    if ( millis() - etxTimeout > 2000 ) return recvETX;       // Return empty string on global timeout
    yield();                                                  // Prevent hardware watchdog triggers
  }

  unsigned long lastCharTime = millis();                      // Read loop with 40ms timeout
  while ( millis() - lastCharTime < 40 ) {                    // Exit if no character is received during 40ms

    if ( serialETX.available() ) {                            // Read byte waiting in the buffer
      char c = serialETX.read();
      lastCharTime = millis();                                // Reset inter-character timer only on valid byte
      if ( c == 0xDF ) c = '*';                               // Replace ° by * (ETX reply with 0xDF)
      if ( len < BUFMASK - 1 && funct != _FLUSH )             // Store into buffer if not flushed
        recvETX[len++] = c;                                   // Add char to buffer if no overflow

      if ( funct == _THRU ) {
        serialAPP.write( c );                                 // Echo ETX reply to APP port
        if ( _LOGLX ) serialDBG.print( c );                   // Echo ETX reply to USB port
      }
    }
    yield();                                                  // Prevent hardware watchdog triggers
  }

  recvETX[len] = '\0';
  return recvETX;
}


// ------------------------------------------------------------
// LX200: Send command to ETX
// ------------------------------------------------------------
void writeETX( char* lxCmd )
{

  if ( lxCmd[0] != '#' )                                      // Cmd from parsing, Add characters removed by parsing
    sprintf_P( sendETX, PSTR( "#:%s#" ), lxCmd );
  else
    strncpy( sendETX, lxCmd, sizeof(sendETX) - 1 );           // Copy raw command to send buffer

  int len = strlen( sendETX );

  // Clear RX FIFO before writing
  unsigned long lastRead = millis();
  while ( serialETX.available() > 0 || ( millis() - lastRead < 3 ) ) {
    if ( serialETX.available() > 0 ) {
      serialETX.read(); lastRead = millis();
    }
  }

  // Filter Cmd for Meade format and fill-in Send buffer
  for ( int i = 0; i < len; i++ ) {
    //    if ( sendETX[i] == '*') sendETX[i] = 223;                 // Replace * by ° (EXT accept both 0xDF or '*')
    serialETX.write( sendETX[i] );                            // Send command to ETX
  }
  serialETX.flush();                                          // Force to send all data
}



/* ************************************************************
   LX200: Functions
 ************************************************************ */

// ------------------------------------------------------------
// LX200: Set HOME and read Autostar Date/Time/Lat/Long
// ------------------------------------------------------------
void setHome()
{
  beep( _OK );
  writeETX( (char*)"#:hC#" );                                                         // Align telescope
  parseLX200Command( (char*)"Gt" ); parseLX200Command( (char*)"Gg" );                 // Get Lat/long, TZ, Date/Time
  parseLX200Command( (char*)"GG" ); parseLX200Command( (char*)"GC" ); parseLX200Command( (char*)"GL" );
}


// ------------------------------------------------------------
// LX200: Set telescope to Parking position
// ------------------------------------------------------------
void setPark()
{
  beep( _WARNING );
  writeETX( (char*)"#:hP#" );                                                         // Align telescope
}


// ------------------------------------------------------------
// LX200: Redirect commands and Intercept unsupported ETX commands
// ------------------------------------------------------------
void parseLX200Command( char* lxCmd )
{
  if ( lxCmd[0] == '\0' ) return;                             // Empty command

  byte   EDxx     = 0;                                        // Pointer to emulate Autostar dialog
  int    degres   = 0;                                        // Degres, Minutes for MM:SS -> MM.T
  double minutes  = 0.0;
  char   stt[255];                                            // Temporary buffer for answers


  /* Handle Meade LX200 ACK connection ping
    --------- */
  // 1. Return: A=AltAz Mode, L=Land Mode, P=Polar Mode, G=German Mount Polar (not # terminated)
  // Mount type not used in Stellarium
  // ----------
  if ( lxCmd[0] == 0x06 ) {
    if ( _LOGLX ) serialDBG.print( F( "#[6] => " ) );
    writeAPP( (char*)( cfg.mount == _MOUNT_ALTAZ ? "A" :  "P" ) ); // Send reply to application
    EDxx = 0;                                                 // Reset Autostar dialog pointer
    return;
  }

  // 2. Find and remove start indicator ':' if any
  // ---------
  if ( lxCmd[0] == ':' ) memmove( lxCmd, lxCmd + 1, strlen( lxCmd ) );

  // 3. Debug print command
  // ----------
  if ( _LOGLX )  {
    serialDBG.print( F( "\n#:" ) ); serialDBG.print( lxCmd ); serialDBG.print( F( "# => " ) );
  }



  // 4. Main LX200 command dictionary mapping
  // ------------------------------------------------------------

  /* USER FORMAT CONTROL (#U)
     --------- */
  // U1. Toggle Long/Short Format (:U#), implicitly accept, no text response required
  // ---------
  if ( strcmp_P( lxCmd, PSTR( "U" ) ) == 0 ) {
    if ( _LOGLX ) serialDBG.println();
  }


  /* Set Telescope alignment mode
     --------- */
  // A1. Set AltAz Alignment Mode(:AA#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "AA" ) ) == 0 ) {
    cfg.mount = _MOUNT_ALTAZ;
  }

  // A2. Set Polar Alignment Mode(:AP#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "AP" ) ) == 0 ) {
    cfg.mount = _MOUNT_POLAR;
  }


  /* Get INFORMATION (#G) from Telescope
     --------- */
  // G1. Request Product Name (:GVP#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GVP" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
    //    sprintf_P( stt, PSTR( "%s#" ), APP_NAME );
    //    writeAPP( stt );
  }

  // G2. Request Hardware/Firmware Version Number (:GVN#) - Format (vx.x)
  // ---------
  else if ( strcmp_P( lxCmd, "GVN" ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
    //    sprintf_P( stt, PSTR( "v%s#" ), VERSION );
    //    writeAPP( stt );
  }

  // G3. Request Firmware Build Date (:GVD#) - Format (Aug 23 2026#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GVD" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
    //    sprintf_P( stt, PSTR( "%s#" ), __DATE__ );
    //    writeAPP( stt );
  }

  // G4. Request Firmware Build Time (:GVT#) - Format (HH:MM:SS#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GVT" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
    //    sprintf_P( stt, PSTR( "%s#" ), __TIME__ );
    //    writeAPP( stt );
  }

  // G5. Request Current Date (:GC#) -> Format (MM/DD/YY#). Month is 0-11.
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GC" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    char* reply = readETX( _GET );                          // Intercept Time to set internal clock
    writeAPP( reply );
    setClockDate( reply );                                  // Set ESP32 clock from MM/DD/YY#
  }

  // G6. Request Local Time (:GL#) -> Returns the Autostar clock value HH:MM:SS#
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GL" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    char* reply = readETX( _GET );                          // Intercept Time to set internal clock
    writeAPP( reply );
    setClockTime( reply );                                  // Set ESP32 clock from HH:MM:SS#
  }

  // G7. Request Greenwich Mean Time Offset (:GG# -> #:SG±HH.T#) ETX accept only ±HH# - For +2H -> -2.0#. Will be updated with Stellarium time Sync
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GG" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    char* reply = readETX( _GET );                          // Intercept Time to set internal clock
    int i = -99;
    if ( sscanf( reply, "%d", &i ) == 1 ) cfg.timezone = i; // Set negative TZ in settings
    sprintf_P( stt, PSTR( "%+02d.0#" ), cfg.timezone );     // Convert ±HH# to -(±HH.T)#
    writeAPP( stt );                                        // Send reply to application (change sign and convert to #:SG±HH#)
  }

  // G8. Request Site Latitude (:Gt#) - Format "±DD*MM#", [+90,-90]
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "Gt" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    char* reply = readETX( _GET );                          // Intercept Lat to update settings
    writeAPP( reply );                                      // Send reply to application (±DD*MM#)
    if ( sscanf( reply, "%d*%lf", &degres, &minutes ) == 2 )
      cfg.siteLat = degres + ( minutes / 60.0 );            // Update site Lat
  }

  // G9. Request Site Longitude (:Gg#) - Format "DDD*MM#", [0,360[ Eastward
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "Gg" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    char* reply = readETX( _GET );                          // Intercept Long to update settings
    writeAPP( reply );                                      // Send reply to application (DDD*MM#)
    if ( sscanf( reply, "%d*%lf", &degres, &minutes ) == 2 )
      cfg.siteLong =  degres + ( minutes / 60.0 );          // Update site Long
  }

  // G10. Request Alignment Status (:GW#)
  // P=Polar, A=AltAz, G=German / T=Tracking, N=Stopped/ 0:not Aligned, 1:1-Star, 2:2-Stars
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "GW" ) ) == 0 ) {
    bool isMoving  = false;                                 //#### To be managed
    bool isAligned = 2;                                     //#### To be managed
    sprintf_P( stt, PSTR( "%s%s%d#" ), ( cfg.mount ? "A" : "P" ), ( isMoving ? "T" : "N" ), isAligned );
    writeAPP( stt );                                        // Send reply to application
  }

  // G11. Request user to press  button -> expect \x0D\x0A\x0D\x0A reply
  // :EK9# -> MODE, :EK68# -> ENTER
  // Stellarium goes up to the Root Menu and goes down to Setup/Telescope/Telescope Model
  // ---------
  else if ( lxCmd[0] == 'E' && lxCmd[1] == 'K' ) {          // Autostar KEY
    writeAPP( (char*)( "\r\n\r\n" ) );
  }

  // G12. Request Autostar display (:ED#) -> expect \x0D\x0A\x0D\x0A reply
  // Stellarium goes up to the Root Menu and goes down to Setup/Telescope/Telescope Model
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "ED" ) ) == 0 ) {
    //    sprintf_P( stt, PSTR( "\r\n%s#" ), list[cfg.model][EDxx] ); // Emulate Autostar dialog
    sprintf_P( stt, PSTR( "#" ) );                          // Emulate Autostar dialog
    writeAPP( stt );                                        // Send reply to application
    EDxx++;
  }

  // G13. Request current Right Ascension (:GR#) --> Format "04:25.5#"
  // ------------------------------------------------------------
  else if ( strcmp_P( lxCmd, PSTR( "GR" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
  }

  // G14. Request current Declination (:GD#) -> Format "+45*30#"
  // ------------------------------------------------------------
  else if ( strcmp_P( lxCmd, PSTR( "GD" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
  }

  /* Set INFORMATION (#S) from astro application
     --------- */
  // S1. Set Greenwich Mean Time Offset (:SG±HH.H#) -> ETX expect only :SG+01# or :SG-02#
  // ---------
  else if ( strncmp_P( lxCmd, PSTR( "SG" ), 2 ) == 0 ) {
    lxCmd[5] = '#'; lxCmd[6] = '\0';                        // Convert SG±HH.H# -> SG±HH#
    cfg.timezone = atoi( lxCmd );                           // Store time zone
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
  }

  // S2. Set Local Time (:SLHH:MM:SS#) -> Sync internal ESP32 software clock
  // ---------
  else if ( strncmp_P( lxCmd, PSTR( "SL"), 2 ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU );                                       // Echo reply
    setClockTime( lxCmd + 2 );                              // Set ESP32 clock (skip SL to keep HH/MM/SS#)
  }

  // S3. Set Current Date (:SCMM/DD/YY#) -> Responds to calendar initialization handshakes
  // Returns a <bool> followed by a <string> hash-mark terminated.
  // ---------
  else if ( strncmp_P( lxCmd, PSTR( "SC" ), 2 ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    delay( 500 );
    readETX( _THRU );                                       // Echo reply (1Updating planetary data        #)
    delay( 500 );
    readETX( _FLUSH );                                      // Clear buffer (2nd ligne of the display)
    setClockDate( lxCmd + 2 );                              // Set ESP32 clock (skip SC to keep MM/DD/YY#)
  }

  // S4. Set Site Latitude (:St±DD*MM#) ->  Format received: St+45*30
  // ---------
  else if ( strncmp_P( lxCmd, PSTR( "St" ), 2 ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU );                                       // Echo reply
    if ( sscanf( lxCmd + 2, "%d*%lf", &degres, &minutes ) == 3 )
      cfg.siteLat = degres + ( minutes / 60.0 );            // Update site Lat
  }

  // S5. Set Site Longitude (:SgDDD*MM#) -> Format received: Sg005*45 or Sg120*15:45 --> Sg120*15
  // ---------
  else if ( strncmp_P( lxCmd, PSTR( "Sg" ), 2 ) == 0 ) {
    if ( strlen( lxCmd ) > 8 ) lxCmd[8] = '\0';             // Ignore seconds
    writeETX( lxCmd );                                      // Send command to ETX with format Sg120*15
    readETX( _THRU );                                       // Echo reply
    if ( sscanf( lxCmd + 2, "%d*%lf", &degres, &minutes ) == 3 )
      cfg.siteLong =  degres + ( minutes / 60.0 );          // Update site Long
  }

  // S6. Set target Right Ascension (:SrHH:MM:SS#) ETX accept only :SrHH:MM.T#)
  // ------------------------------------------------------------
  else if ( strncmp_P( lxCmd, PSTR( "Sr" ), 2 ) == 0 ) {
    int h = 0, m = 0, s = 0;
    if ( sscanf( lxCmd + 2, "%d:%d:%d#", &h, &m, &s ) == 3 ) { // Handles both HH:MM:SS# and HH:MM.T# inputs via standard parsing
      sprintf_P( stt, PSTR( "Sr%02d:%04.1f#" ), h, m + ( s / 60.0 ) );
      writeETX( stt );                                      // Send command to ETX with format Sr08:15.7#
    } else
      writeETX( lxCmd );                                    // Send command to ETX with initial format
    readETX( _THRU );                                       // Echo reply
  }


  // S7. Set target Declination (:Sd±DD*MM:SS# / :Sd±DD:MM:SS#) ETX accept only (:Sd±DD*MM#)
  // ------------------------------------------------------------
  else if ( strncmp_P( lxCmd, PSTR( "Sd" ), 2 ) == 0 ) {
    char sign = '+', car;
    int d = 0, m = 0, s = 0;
    if ( sscanf( lxCmd + 2, "%c%d%c%d:%d#", &sign, &d, &car, &m, &s ) == 5 ) { // Handles both ±DD*MM:SS# (or :SS#) and ±DD*MM# formats natively
      sprintf_P( stt, PSTR( "Sd%c%02d*%02d#" ), sign, d, m );
      writeETX( stt );                                      // Send command to ETX with format Sd+20*15#
    } else
      writeETX( lxCmd );                                    // Send command to ETX with initial format
    readETX( _THRU );                                       // Echo reply
  }

  /* GOTO MOVEMENT (#M)
     --------- */
  // M1. Initiate Slew Pointing GoTo Target RA/Dec (#MS#) -> Trigger telescope movement towards target
  // Return: 0 or <string>#0 if OK, 1 for 'lower' limit, 2 for 'upper' limit.
  // e.g. "   M31    EX GAL MAG 3.5 SZ178.0'#0"
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "MS") ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU );                                       // Echo reply
  }

  // M2. Alternate Slew Trigger Target Alt/Az (#MA#) -> Alternate universal Goto trigger used by older desktop planetariums
  // NOT USED BY STELLARIUM !!!!!!! TBC
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "MA" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU );                                       // Echo reply
  }

  // M3. Move Direction Commands (:Mn#, :Ms#, :Me#, :Mw#)
  // ---------
  // Mn: Move Up
  else if ( strcmp_P( lxCmd, PSTR( "Mn" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }
  // Ms#: Move South (Declination decreases/moves South)
  else if ( strcmp_P( lxCmd, PSTR( "Ms" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }
  // Me: Move East
  else if ( strcmp_P( lxCmd, PSTR( "Me" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }
  // Mw: Move West
  else if ( strcmp_P( lxCmd, PSTR( "Mw" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }


  /* MOTOR SPEED RATE CONTROL (#R)
     --------- */
  // R1. Set Guide Rate (:RG#) -> Map to ETXSlew2 (2x Sidereal)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "RG" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }

  // R2. Set Centering Rate (:RC#) -> Map to ETXSlew5 (32x Sidereal)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "RC" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }

  // R3. Set Find/Map Rate (:RM#) -> Map to ETXSlew7 (0.5°/s)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "RM" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }

  // R4. Set Max Slew Rate (:RS#) -> Map to ETXSlew10 (240x Sidereal - Max for ETX-70)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "RS" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }


  /* TRACKING FREQUENCY CONTROL (#T)
    --------- */
  // T1. Default Tracking (:TQ#)
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "TQ" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }

  // T2. Set Lunar Tracking Rate (:TL#) -> Inject native physical LUNARRATE
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "TL" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _FLUSH );                                      // Echo reply
  }


  /* DISTANCE (#D)
     --------- */
  // D1. Request Slew Distance Status / Is Moving (:D#)
  // Return : Moving= "|#", Stopped= "#"
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "D" ) ) == 0 ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU);                                        // Echo reply
  }


  /* SYNC CONTROL (#C) !!!!! CRASH AUTOSTAR !!!!!
     --------- */
  // C1. Synchronize Mount coordinates (:CM#) -> Recalibrate hardware position registers
  // Reply nothing or ...
  // ---------
  else if ( strcmp_P( lxCmd, PSTR( "CM" ) ) == 0 ) {
    //    writeETX( lxCmd );                                      // Send command to ETX
    //    delay( 1000 );                                          // Wait for reply
    //    readETX( _THRU );                                       // Echo reply
    //    delay( 1000 );                                          // Flush remaining
    //    readETX( _FLUSH );                                      // Empty buffer
    writeAPP( (char*)"#0" );
  }

  /* QUIT (#Q)
     --------- */
  // Q1. Emergency deceleration stop :Q#, :Qn# (North), :Qs# (South), :Qe# (East), :Qw# (West)
  // ---------
  else if ( lxCmd[0] == 'Q' ) {
    writeETX( lxCmd );                                      // Send command to ETX
    readETX( _THRU );                                       // Echo reply
  }

}


/* EOF */
