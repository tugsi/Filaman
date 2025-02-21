#ifndef OTA_H
#define OTA_H

#include <ESPAsyncWebServer.h>

// Update size unknown constant, falls nicht bereits definiert
#ifndef UPDATE_SIZE_UNKNOWN
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#endif

void stopAllTasks();
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void checkForStagedUpdate();

#endif