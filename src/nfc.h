#pragma once

#include <Arduino.h>
#include <Adafruit_PN532.h>

void startNfc();
void startWriteJsonToTag(const char* payload);
void writeJsonToTag(void *parameter);
void scanRfidTask(void * parameter);
void processTag(uint8_t *uid, uint8_t uidLength, uint8_t readerNumber);
void processNfcData(uint8_t *data, String tagId);
bool decodeNdefAndReturnJson(const byte* encodedMessage);

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String spoolId;
extern volatile uint8_t hasReadRfidTag;
extern volatile bool pauseBambuMqttTask;