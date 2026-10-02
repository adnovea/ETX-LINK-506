/**********************************************************************

  header.h - Header file
  Copyright ©2017-2025 AdNovea®

  This original work is provided ASIS.

***********************************************************************/



/************************************************************
  LBRARIES, CONSTANTES, VARIABLES, STRUCTURES
 ************************************************************/
// Related libraries
#include <math.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"    // Ignore DEPRECATED compilation warnings (required for Bluetooth SPP)
#include "BluetoothSerial.h"
#include <EEPROM.h>                                           // Support for EEPROM (save permanent params)


// --- CUSTOM PARAMS
#define         APP_NAME            "ETX Link 506"            // Device description
#define         VERSION             "1.0.1"                   // Firmware version
#define         SERIALNUMBER        "2026090001"              // Device S/N
#define         BAUDRATE            115200                    // USB Serial debug baud rate
#define         BAUDLX200           9600                      // AUX Serial ETX baud rate


// --- OUTPUTS
#define         LED_BUILTIN         2                         // On-board LED for BLT
#define         LED_COMMS           4                         // Activity LED
#define         PIN_BUZZ            32                        // Buzzer piezo with PWM


// --- SERIAL LINKS
#define         USB_TX              1                         // Serial0 (USB) Tx=GPIO-01
#define         USB_RX              3                         // Serial0 (USB) Rx=GPIO-03
#define         AUX_TX              18                        // Serial1 (AUX) Tx=GPIO-19
#define         AUX_RX              19                        // Serial1 (AUX) Rx=GPIO-18
#define         ETX_TX              16                        // Serial2 (ETX) Tx=GPIO-17 for #506 CCS
#define         ETX_RX              17                        // Serial2 (ETX) Rx=GPIO-16 for #506 CCS


// --- BUTTON/JUMPER with internal pullup
#define         PIN_BUT1            15                        // Button move telescope Up


// ----------------- Interface -----------------
#define         _READY              1                         // Buzzer preset tones
#define         _WARNING            2
#define         _FAIL               3
#define         _OK                 4
#define         _BEEP               5
#define         _BEEP2              6
#define         _BEEP3              7

const char      _BAR[22]            = "#--------------------";

// ----------------- Logs -----------------
#define LOG_MSG_SIZE 300
char log_msg[LOG_MSG_SIZE];                                   // log message to sprintf to
bool            _LOGS               = true;                   // Print standard log messages
bool            _LOGLX              = true;                   // Print LX200 log messages


// ----------------- Clock -----------------
// sec(0 à 59), Min(0 à 59), Hours(0 à 23), Day of month(1 à 31), Month 0=january,1=February, YEAR - 1900, Day of week, Day of year, daylight saving
struct timeval  now;                                          // ESP32 clock
time_t          utc_ts;                                       // Store GMT Unix timestamp in absolute UTC (seconds since Jan 1, 1970)
struct tm       locTime;                                      // Local time structure, synchronized using cfg.timezone for display and user logs
struct tm       gmtTime;                                      // UTC/GMT time structure used for core astronomical computations


// ---------------- COMMS ----------------
#define         serialDBG   Serial                            // UART0 = Moniteur USB
HardwareSerial  serialAUX(1);
#define         serialETX   Serial2                           // UART2 = Port ETX (Inverted logic for #506 module)
BluetoothSerial serialBLT;                                    // Stellarium requise Bluetooth SPP but must Ignore deprecated warning because
Stream*         _serialAPP_Ptr      = nullptr;                // Hidden pointer fro serialAPP
#define         serialAPP (*_serialAPP_Ptr)                   // Port to connect astro application


// ---------------- LX200 ----------------
#define         _MOUNT_POLAR        0                         // AltAz mount
#define         _MOUNT_ALTAZ        1                         // Equatorial mount
#define         BUFF_MAX_LEN        250                       // 250 max length
#define         BUFMASK             BUFF_MAX_LEN-1                              // Index wraps to 0

#define         _FLUSH              0                         // Ignore reply
#define         _THRU               1                         // Passthru reply to APP
#define         _GET                2                         // Return reply

//struct __attribute__((packed)) structBuffer {
struct structBuffer {
  char          text[BUFF_MAX_LEN];
  uint8_t       len;
};
structBuffer   recvAPP;                                       // APP Rx buffer
char            recvETX[BUFF_MAX_LEN];                        // ETX Rx buffer
char            sendETX[BUFF_MAX_LEN];                        // ETX Tx buffer

/*
// Array holding the list of telescopes using the Autostar #494 controller
const char* const list[12][11] PROGMEM = {
  { "Altazimuth", "ETX-60 AT",   "350", "", "", "", "", "", "", "", "" }, // ETX Series (Fork Altazimuth)
  { "Setup", "Align", "Date", "Time", "Daylight Saving", "Telescope", "Telescope Model", "ETX-70", "+350", "", "" },
  { "Altazimuth", "ETX-80 AT",   "400", "", "", "", "", "", "", "", "" },
  { "DS-2000",    "DS-2060 AT",  "700", "", "", "", "", "", "", "", "" }, // DS-2000 Series (Single-arm Altazimuth GoTo)
  { "DS-2000",    "DS-2070 AT",  "700", "", "", "", "", "", "", "", "" },
  { "DS-2000",    "DS-2076 AT",  "700", "", "", "", "", "", "", "", "" },
  { "DS-2000",    "DS-2080 AT",  "800", "", "", "", "", "", "", "", "" },
  { "DS-2000",    "DS-2090 AT",  "800", "", "", "", "", "", "", "", "" },
  { "DS-2000",    "DS-2114 AT", "1000", "", "", "", "", "", "", "", "" }, // Newton with corrector
  { "DS-2130",    "DS-2130 AT", "1000", "", "", "", "", "", "", "", "" }, // Newton
  { "DSX",        "DSX-90",     "1250", "", "", "", "", "", "", "", "" }, // DSX Series Maksutov-Cassegrain
  { "DSX",        "DSX-125",    "1900", "", "", "", "", "", "", "", "" }
};
*/

// ----------------- Structure for Settings -----------------
#define CONFIG_EEPROM_ADDR 0x00                               // EEPROM offset
struct Settings {

  // Change ID value to force reload of default values
  byte      ID                  = 0x00;                       // Change to force default against EEPROM (Keep as 1st EEPROM byte)

  // Interface & ports
  int       buzzer              = 5;                          // Buzzer volume [0-5]
  bool      blt                 = true;                       // USB 0:AUX, 1:BLT

  // Location
  double    siteLat             = 45.0;                       // Site latitude en d° (+:North, -:South)
  double    siteLong            = 0.0;                        // Site longitude en d° (-:West, +:East)
  int       timezone            = 0;                          // Time zone

  // Teslescope
  byte      model               = 1;                          // Telescope model
  byte      mount               = _MOUNT_ALTAZ;               // Telescope mount type: 0=Polar, 1=AltAz

} cfg;


/*  EOF */
