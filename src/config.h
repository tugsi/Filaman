#pragma once

#include <Arduino.h>

// ***** PN532 (RFID)
//#define PN532_SCK   18
//#define PN532_MOSI  23
//#define PN532_SS    5
//#define PN532_MISO  19
// ***** PN532

extern const uint8_t LOADCELL_DOUT_PIN;
extern const uint8_t LOADCELL_SCK_PIN;
extern const uint8_t calVal_eepromAdress;
extern const uint16_t SCALE_LEVEL_WEIGHT;

extern const int8_t OLED_RESET;
extern const uint8_t SCREEN_ADDRESS;
extern const uint8_t SCREEN_WIDTH;
extern const uint8_t SCREEN_HEIGHT;
extern const uint8_t OLED_TOP_START;
extern const uint8_t OLED_TOP_END;
extern const uint8_t OLED_DATA_START;
extern const uint8_t OLED_DATA_END;

extern const char* apiUrl;
extern const uint8_t webserverPort;

extern uint8_t autoSetBambuAmsCounter;

extern const unsigned char wifi_on[];
extern const unsigned char wifi_off[];
extern const unsigned char cloud_on[];
extern const unsigned char cloud_off[];

extern const unsigned char icon_failed[];
extern const unsigned char icon_success[];
extern const unsigned char icon_transfer[];
extern const unsigned char icon_loading[];

extern uint8_t rfidTaskCore;
extern uint8_t rfidTaskPrio;

extern uint8_t rfidWriteTaskPrio;

extern uint8_t mqttTaskCore;
extern uint8_t mqttTaskPrio;

extern uint8_t scaleTaskCore;
extern uint8_t scaleTaskPrio;

extern uint16_t defaultScaleCalibrationValue;

#define PN532_SCK  (18)
#define PN532_MISO (19)
#define PN532_MOSI (23)
#define PN532_CS1  (5)
#define PN532_CS2  (4)