#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"

// Magic byte patterns für verschiedene Image-Typen
const uint8_t FIRMWARE_MAGIC = 0xE9;
const uint8_t ESP_MAGIC = 0xE9;

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static size_t contentLength = 0;
    static bool isFullImage = false;
    static uint32_t currentOffset = 0;
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update size: %u bytes\n", contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Prüfe Magic Byte für Image-Typ
        if (data[0] == ESP_MAGIC) {
            // Normales Firmware Image oder full.bin
            isFullImage = (contentLength > 0x3D0000);
            
            if (!Update.begin(contentLength)) {
                Serial.printf("Not enough space: %u required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                return;
            }
            
            Serial.println(isFullImage ? "Full image update started" : "Firmware update started");
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid image format\"}");
            return;
        }
        currentOffset = 0;
    }
    
    // Schreibe Daten
    if (Update.write(data, len) != len) {
        String errorMsg = Update.errorString();
        if (errorMsg != "No Error") {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
            return;
        }
    }
    
    currentOffset += len;

    if (final) {
        if (Update.end(true)) {
            Serial.println("Update complete");
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
            delay(1000);
            ESP.restart();
        } else {
            String errorMsg = Update.errorString();
            if (errorMsg != "No Error") {
                Update.printError(Serial);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Update failed: " + errorMsg + "\"}");
            } else {
                request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
                delay(1000);
                ESP.restart();
            }
        }
    }
}

