#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static size_t contentLength = 0;
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update size: %u bytes\n", contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Determine if this is a full image (firmware + SPIFFS) or just firmware
        bool isFullImage = (contentLength > 0x3D0000); // SPIFFS starts at 0x3D0000

        if (!Update.begin(contentLength, isFullImage ? U_FLASH : U_SPIFFS)) {
            Serial.printf("Not enough space: %u required\n", contentLength);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
            return;
        }
        
        Serial.println(isFullImage ? "Full image update started" : "Firmware update started");
    }
    
    // Write chunk to flash
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update\"}");
        return;
    }

    if (final) {
        if (Update.end(true)) {
            Serial.println("Update complete");
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
            delay(1000);
            ESP.restart();
        } else {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Update failed\"}");
        }
    }
}

