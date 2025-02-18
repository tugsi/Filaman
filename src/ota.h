#ifndef OTA_H
#define OTA_H

#include <ESPAsyncWebServer.h>

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

#endif