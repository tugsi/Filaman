#ifndef OTA_H
#define OTA_H

#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>

void handleUpdate(AsyncWebServer &server);

#endif