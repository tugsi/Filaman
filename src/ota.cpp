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

        if (isFullImage) {
            // For full images, we need to make sure we have enough space and properly partition it
            if (!Update.begin(ESP.getFreeSketchSpace(), U_FLASH)) {
                Serial.printf("Not enough space for full image: %u bytes required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Full image updates are not supported via OTA. Please use USB update for full images.\"}");
                return;
            }
        } else {
            // For firmware-only updates
            if (!Update.begin(contentLength, U_FLASH)) {
                Serial.printf("Not enough space: %u required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available for firmware update\"}");
                return;
            }
        }
        
        Serial.println(isFullImage ? "Full image update started" : "Firmware update started");
    }
    
    // Write chunk to flash
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        String errorMsg = Update.errorString();
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
        return;
    }

    if (final) {
        if (Update.end(true)) {
            Serial.println("Update complete");
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
            delay(1000);
            ESP.restart();
        } else {
            String errorMsg = Update.errorString();
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Update failed: " + errorMsg + "\"}");
        }
    }
}

