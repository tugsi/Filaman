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
    static uint8_t *spiffsBuffer = nullptr;
    static size_t spiffsSize = 0;
    static const uint32_t SPIFFS_START = 0x310000;  // Standard SPIFFS start address in full.bin
    
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
            
            if (isFullImage) {
                // Allocate buffer for SPIFFS data if it's a full image
                spiffsSize = 0x0F0000;  // Standard SPIFFS size
                spiffsBuffer = (uint8_t*)malloc(spiffsSize);
                if (!spiffsBuffer) {
                    Serial.println("Failed to allocate SPIFFS buffer");
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
                    return;
                }
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid image format\"}");
            return;
        }
        currentOffset = 0;
    }
    
    // Handle SPIFFS data collection for full image
    if (isFullImage && spiffsBuffer) {
        uint32_t spiffsOffset = currentOffset - SPIFFS_START;
        if (currentOffset >= SPIFFS_START && spiffsOffset < spiffsSize) {
            size_t copyLen = min(len, spiffsSize - spiffsOffset);
            if (spiffsOffset + len > spiffsSize) {
                copyLen = spiffsSize - spiffsOffset;
            }
            memcpy(spiffsBuffer + spiffsOffset, data, copyLen);
        }
    }
    
    // Schreibe Daten
    if (Update.write(data, len) != len) {
        String errorMsg = Update.errorString();
        if (errorMsg != "No Error") {
            Update.printError(Serial);
            if (spiffsBuffer) free(spiffsBuffer);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
            return;
        }
    }
    
    currentOffset += len;

    if (final) {
        if (Update.end(true)) {
            Serial.println("Firmware update complete");
            
            if (isFullImage && spiffsBuffer) {
                Serial.println("Starting SPIFFS update");
                if (!SPIFFS.begin(true)) {
                    Serial.println("SPIFFS mount failed");
                }
                
                // Update SPIFFS
                if (Update.begin(spiffsSize, U_SPIFFS)) {
                    if (Update.write(spiffsBuffer, spiffsSize) != spiffsSize) {
                        Serial.println("SPIFFS Write Failed");
                        free(spiffsBuffer);
                        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS update failed\"}");
                        return;
                    }
                    if (!Update.end(true)) {
                        Serial.println("SPIFFS End Failed");
                        free(spiffsBuffer);
                        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS finish failed\"}");
                        return;
                    }
                }
                free(spiffsBuffer);
                Serial.println("SPIFFS update complete");
            }
            
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
            delay(1000);
            ESP.restart();
        } else {
            if (spiffsBuffer) free(spiffsBuffer);
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

