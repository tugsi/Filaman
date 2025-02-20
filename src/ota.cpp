#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"

// Magic byte patterns für verschiedene Image-Typen
const uint8_t FIRMWARE_MAGIC = 0xE9;
const uint8_t ESP_MAGIC = 0xE9;

void stopTasks() {
    // Stop all tasks
    vTaskSuspend(NULL);
}

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static size_t contentLength = 0;
    static bool isFullImage = false;
    static uint32_t currentOffset = 0;
    static uint8_t *spiffsBuffer = nullptr;
    static size_t spiffsSize = 0;
    static const uint32_t SPIFFS_START = 0x310000;  // SPIFFS start in full.bin
    
    stopTasks();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    if (!index) {
        // Reset static variables
        if (spiffsBuffer) {
            free(spiffsBuffer);
            spiffsBuffer = nullptr;
        }
        
        contentLength = request->contentLength();
        Serial.printf("Update size: %u bytes\n", contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (data[0] == ESP_MAGIC) {
            isFullImage = (contentLength > 0x3D0000);
            
            // Korrektur: Bei full.bin die gesamte Größe verwenden
            if (!Update.begin(contentLength, U_FLASH)) {
                Serial.printf("Not enough space: %u required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                return;
            }
            
            Serial.println(isFullImage ? "Full image update started" : "Firmware update started");
            
            if (isFullImage) {
                spiffsSize = 0x0F0000;  // 960KB SPIFFS size
                spiffsBuffer = (uint8_t*)malloc(spiffsSize);
                if (!spiffsBuffer) {
                    Serial.println("Failed to allocate SPIFFS buffer");
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
                    return;
                }
                memset(spiffsBuffer, 0xFF, spiffsSize); // Initialize buffer with 0xFF
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid image format\"}");
            return;
        }
        currentOffset = 0;
    }
    
    if (isFullImage) {
        // Zuerst vollständiges Firmware-Update durchführen
        if (Update.write(data, len) != len) {
            String errorMsg = Update.errorString();
            if (errorMsg != "No Error") {
                Update.printError(Serial);
                if (spiffsBuffer) free(spiffsBuffer);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing firmware: " + errorMsg + "\"}");
                return;
            }
        }
        
        // SPIFFS-Daten separat sammeln
        if (currentOffset >= SPIFFS_START) {
            uint32_t spiffsOffset = currentOffset - SPIFFS_START;
            if (spiffsOffset < spiffsSize) {
                size_t copyLen = min(len, spiffsSize - spiffsOffset);
                memcpy(spiffsBuffer + spiffsOffset, data, copyLen);
                Serial.printf("Collecting SPIFFS data at offset %u, length %u\n", spiffsOffset, copyLen);
            }
        }
    } else {
        // Normal firmware update
        if (Update.write(data, len) != len) {
            String errorMsg = Update.errorString();
            if (errorMsg != "No Error") {
                Update.printError(Serial);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
                return;
            }
        }
    }
    
    currentOffset += len;

    if (final) {
        if (Update.end(true)) {
            Serial.println("Firmware update complete");
            
            if (isFullImage && spiffsBuffer) {
                Serial.println("Starting SPIFFS update");
                
                // Unmount SPIFFS first
                if (SPIFFS.begin()) {
                    SPIFFS.end();
                }
                
                // Update SPIFFS
                if (!Update.begin(spiffsSize, U_SPIFFS)) {
                    Serial.println("Could not begin SPIFFS update");
                    free(spiffsBuffer);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS update initialization failed\"}");
                    return;
                }
                
                if (Update.write(spiffsBuffer, spiffsSize) != spiffsSize) {
                    Serial.println("SPIFFS Write Failed");
                    Update.printError(Serial);
                    free(spiffsBuffer);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS write failed\"}");
                    return;
                }
                
                if (!Update.end(true)) {
                    Serial.println("SPIFFS End Failed");
                    Update.printError(Serial);
                    free(spiffsBuffer);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS finish failed\"}");
                    return;
                }
                
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
                Serial.println("SPIFFS update complete");
                
                // Verify SPIFFS
                if (!SPIFFS.begin(true)) {
                    Serial.println("SPIFFS mount after update failed");
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS verification failed\"}");
                    return;
                }
                SPIFFS.end();
            }
            
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
            delay(1000);
            ESP.restart();
        } else {
            if (spiffsBuffer) {
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
            }
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

