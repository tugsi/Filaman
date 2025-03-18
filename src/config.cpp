#include "config.h"

// ################### Config Bereich Start
// ***** PN532 (RFID)
//#define PN532_SCK   18
//#define PN532_MOSI  23
//#define PN532_SS    5
//#define PN532_MISO  19
// ***** PN532

// ***** HX711 (Waage)
// HX711 circuit wiring
const uint8_t LOADCELL_DOUT_PIN = 16; //16;
const uint8_t LOADCELL_SCK_PIN = 17; //17;
const uint8_t calVal_eepromAdress = 0;
const uint16_t SCALE_LEVEL_WEIGHT = 500;
uint16_t defaultScaleCalibrationValue = 430;
// ***** HX711

// ***** Display
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// On an ESP32:   21(SDA),  22(SCL)
const int8_t OLED_RESET = -1; // Reset pin # (or -1 if sharing Arduino reset pin)
const uint8_t SCREEN_ADDRESS = 0x3C; ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
const uint8_t SCREEN_WIDTH = 128; // OLED display width, in pixels
const uint8_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
const uint8_t OLED_TOP_START = 0;
const uint8_t OLED_TOP_END = 16;
const uint8_t OLED_DATA_START = 17;
const uint8_t OLED_DATA_END = SCREEN_HEIGHT;
// ***** Display

// ***** Webserver
const uint8_t webserverPort = 80;
// ***** Webserver

// ***** API
const char* apiUrl = "/api/v1";
// ***** API

// ***** Bambu Auto Set Spool
uint8_t autoSetBambuAmsCounter = 60;
// ***** Bambu Auto Set Spool

// ***** Task Prios
uint8_t rfidTaskCore = 1;
uint8_t rfidTaskPrio = 1;

uint8_t rfidWriteTaskPrio = 1;

uint8_t mqttTaskCore = 1;
uint8_t mqttTaskPrio = 1;

uint8_t scaleTaskCore = 0;
uint8_t scaleTaskPrio = 1;
// ***** Task Prios
