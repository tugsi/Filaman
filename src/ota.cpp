#include <Arduino.h>
#include "ota.h"
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPUpdateServer.h>

ESPAsyncHTTPUpdateServer updateServer;

void setupOTA(AsyncWebServer &server) {
    updateServer.setup(&server, "/ota", "admin", "admin");
    //updateServer.setup(&server);
}
