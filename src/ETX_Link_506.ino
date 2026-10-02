/**********************************************************************

    Name:       ETX Link 506.ino
    Author:     ADNOVEA® - 2005-2016

  SCOPE
  ==========================
  ESP-32
  The module was developped to interface Stellarium Plus and Meade telescope using the #506 module connected to AUX telescope socket.
  Stellarium can interface directly using LX200 protocol through Bluetooth SSP.

  LED & BUTTON
  ==========================
    Factory reset                     At startup press button during 10s until the LED flash twice/2nd beep
    Align to HOME                     During operation short press button to enter alignment sequence (beep)
    Park telescope                    During operation press button during 3s until the LED flash /1st beep
    Reset                             During operation press button during 10s until the LED turns off /1st beep

  USAGE
  ==========================
    START:
    1. Power on the telescope
    2. Set the Date/Time on the Autostar (and position if not yet done)
    3. Set the telescope in HOME position (Tube leveled and aligned to North)
    4. On the Autostar proceed with the stars alignement
    5. Depress the ETX Link button until the 1st beep to set HOME.
    6. Start and connect Stellarium Plus for mobile to ETX Link using Bluetooth
    7. Ensure Time and Position are synced (otherwise swicth on/off corresponding switch)

    END:
    1. Depress the ETX Link button until the 2nd beep to park the telescope.
    2. Power off the telescope
    3. Disconnect and Quit Stellarium Plus

  COMPILATION
  ==========================
    ESP32 WIFI D1 MINI (ESP-IDF 5.5.x)
    Sketch: 82%, Variables: 12%

    Card configuration:
    -------------------
      - Card:                   ESP32 WROOM DA Module
      - Upload Speed:           921600
      - CPU:                    240 MHz
      - Flash Frequency:        80 MHz
      - Flash size:             4 MB ( 32MB )
      - Partition scheme:       Default 4MB with SPIFFS (1.2MB APP/1.5MB SPIFFS)
      - Programmer:             Esptool

      /ETX_Link/
        │
        ├── ETX_Link.ino      ← Main file
        ├── header.h          ← Header file
        ├── utilities.ino     ← Utility functions
        └── lx200.ino         ← LX200 protocol management

  HARDWARE
  ==========================
  Serial USB            for debugging
  Serial 1              AUX serial connection for LX200
  Serial 2              #506 CCS Serial connection

  CHANGE LOGS
  ==========================
    1.0.1     2026-09-29    Disable #:CM# Sync command
    1.0.0     2026-09-11    Initial and debugging release for ESP-32

  TROUBLESHOOTING
  ==========================
   Still under development


***********************************************************************/



// Check if project is for ESP32
#ifdef ARDUINO_ARCH_ESP32


// Header files
#include "01_header.h"


// ------------------------------------------------------------
//  Initialize
// ------------------------------------------------------------
void setup()
{
  //  🟢 Initialize serial for debugging (USB)
  serialDBG.begin( BAUDRATE, SERIAL_8N1, USB_RX, USB_TX );    // USB serial for debugging
  delay( 10 );
  showVer();

  //  🟢 Setup and initialize I/Os
  pinMode( LED_BUILTIN, OUTPUT );                             // BLT LED
  pinMode( LED_COMMS,   OUTPUT );                             // COMMS LED
  pinMode( PIN_BUZZ,    OUTPUT );                             // Buzzer
  pinMode( PIN_BUT1,    INPUT_PULLUP );                       // User button #1

  digitalWrite( LED_COMMS, LOW );
  digitalWrite( PIN_BUZZ,  LOW );

  //  🟢 Init buzzer
  ledcAttach( PIN_BUZZ, 1000, 8 );                            // Set PWM @ 1kHz with 8-bit resolution

  //  🟢 Load parameters
  log_message( F( "\n 🟢 Settings" ), _LOGS );
  EEPROM.begin( 512 );                                        // Allocate virtual EEPROM space for settings
  if ( digitalRead( PIN_BUT1 ) ) {
    ee_read( CONFIG_EEPROM_ADDR );                            // if BUT depressed ignore EEPROM and reset to default
    log_message( F( " Loaded" ), _LOGS );
  } else {
    beep( _BEEP );
    log_message( F( " Factory reset" ), _LOGS );
  }
  while ( !digitalRead( PIN_BUT1 ) );                         // Wait for button released

  //  🟢 Initialize timers, counts and clock
  log_message( F( "\n 🟢 Clock system\n Initialiized" ), _LOGS );
  now.tv_sec    = 0; now.tv_usec = 0;                         // Timeval structure to store UTC timestamp (seconds & µsec) @ 01/01/2026 00:00:00 +00.0
  settimeofday( &now, NULL );                                 // Set time configuration to initial datatime

  //  🟢 Initialize Bluetooth or AUX port to connect application
  log_message( F( "\n 🟢 Communications" ), _LOGS );
  if ( cfg.blt ) {
    _serialAPP_Ptr  = &serialBLT;                             // Application on Serial Bluetooth
    sprintf_P( log_msg, PSTR( " Bluetooth connection %s - %s"), serialBLT.begin( APP_NAME ) ?  "Ready" : "failed!", APP_NAME );
    log_message( log_msg, _LOGS );
  } else  {
    serialAUX.begin( BAUDLX200, SERIAL_8N1, AUX_RX, AUX_TX ); // AUX serial for LX200 @ 9600 bauds
    _serialAPP_Ptr = &serialAUX;                              // Application on Serial port AUX
    log_message( F( " AUX connection Ready" ), _LOGS );
  }
  digitalWrite( LED_BUILTIN, cfg.blt ? HIGH : LOW );          // Switch ON WIFI onbard LED

  //  🟢 Init ETX serial port
  serialETX.begin( BAUDLX200, SERIAL_8N1, ETX_RX, ETX_TX, true ); // Inverted logic for #506 module @ 9600 bauds
  log_message( F( " ETX Serial connection Ready" ), _LOGS );

  //  🟢 READY
  log_message( F( "\n 🟢 Setup Complete\n ✅ READY\n Type ? for help\n" ), _LOGS );
  beep( _READY );                                             // Beep READY
  log_message( F( " Listening for commands...\n" ), _LOGS );
  _LOGLX = false;
}

// ------------------------------------------------------------
// Main loop
// ------------------------------------------------------------
void loop()
{
  //  🟢 LX200: Intercept commands from comms ports and send replies
  readAPP();

  //  🟢 UTILITY: console, buttons
  readConsole();                                              // Read console commands
  readButtons();                                              // Check buttons

  yield();                                                    // REQUIRED (eg. on ESP-01 for OTA)
}


#else
#error "Error: This program is only for ESP32."
#endif


/*  EOF */
