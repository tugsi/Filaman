#ifndef NFC_H
#define NFC_H

#include <Arduino.h>

void startNfc();
void scanRfidTask(void * parameter);
void startWriteJsonToTag(const char* payload);

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String spoolId;
extern volatile uint8_t hasReadRfidTag;
extern volatile bool pauseBambuMqttTask;

#endif