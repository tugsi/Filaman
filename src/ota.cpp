#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"
#include "bambu.h"
#include "scale.h"
#include "nfc.h"

// Magic byte patterns für verschiedene Image-Typen
const uint8_t FIRMWARE_MAGIC = 0xE9;
const uint8_t ESP_MAGIC = 0xE9;
static bool tasksAreStopped = false;

void stopAllTasks() {
    // Stop all tasks
    Serial.println("Stopping RFID Reader");
    if (RfidReaderTask) vTaskSuspend(RfidReaderTask);
    Serial.println("Stopping Bambu");
    if (BambuMqttTask) vTaskSuspend(BambuMqttTask);
    Serial.println("Stopping Scale");
    if (ScaleTask) vTaskSuspend(ScaleTask);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    Serial.println("All tasks stopped");
}

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static size_t contentLength = 0;
    static bool isFullImage = false;
    static uint32_t currentOffset = 0;
    static uint8_t *spiffsBuffer = nullptr;
    static size_t spiffsSize = 0;
    static const uint32_t SPIFFS_START = 0x3D0000;  // Korrigierter SPIFFS start address aus partitions.csv
    static const uint32_t SPIFFS_SIZE = 0x30000;   // Korrigierte SPIFFS size aus partitions.csv
    
    // Stop all tasks to save resources
    if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
        stopAllTasks();
        tasksAreStopped = true;
    }

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
            isFullImage = (contentLength > SPIFFS_START);
            
            if (isFullImage) {
                if (!Update.begin(SPIFFS_START, U_FLASH)) {
                    Serial.printf("Not enough space for firmware: %u required\n", SPIFFS_START);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                    return;
                }
                
                spiffsSize = SPIFFS_SIZE;
                spiffsBuffer = (uint8_t*)malloc(spiffsSize);
                if (!spiffsBuffer) {
                    Serial.println("Failed to allocate SPIFFS buffer");
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
                    return;
                }
                memset(spiffsBuffer, 0xFF, spiffsSize);
                Serial.println("Full image update started");
            } else {
                if (!Update.begin(contentLength, U_FLASH)) {
                    Serial.printf("Not enough space: %u required\n", contentLength);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                    return;
                }
                Serial.println("Firmware update started");
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid image format\"}");
            return;
        }
        currentOffset = 0;
    }
    
    if (isFullImage) {
        // Firmware Teil
        if (currentOffset < SPIFFS_START) {
            size_t writeLen = min(len, (size_t)(SPIFFS_START - currentOffset));
            if (Update.write(data, writeLen) != writeLen) {
                String errorMsg = Update.errorString();
                if (errorMsg != "No Error") {
                    Update.printError(Serial);
                    if (spiffsBuffer) free(spiffsBuffer);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing firmware: " + errorMsg + "\"}");
                    return;
                }
            }
        }
        
        // SPIFFS Teil
        if (currentOffset + len > SPIFFS_START) {
            uint32_t spiffsOffset = (currentOffset >= SPIFFS_START) ? 
                                  (currentOffset - SPIFFS_START) : 0;
            uint32_t dataOffset = (currentOffset < SPIFFS_START) ? 
                                (SPIFFS_START - currentOffset) : 0;
            
            if (spiffsOffset < spiffsSize) {
                size_t copyLen = min(len - dataOffset, spiffsSize - spiffsOffset);
                memcpy(spiffsBuffer + spiffsOffset, data + dataOffset, copyLen);
                Serial.printf("Collecting SPIFFS data: offset=%u, len=%u\n", spiffsOffset, copyLen);
            }
        }
    } else {
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
                
                if (SPIFFS.begin(true)) {
                    SPIFFS.end();
                }
                
                if (!Update.begin(spiffsSize, U_SPIFFS)) {
                    Serial.println("Could not begin SPIFFS update");
                    free(spiffsBuffer);
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS update initialization failed\"}");
                    return;
                }
                
                Serial.printf("Writing SPIFFS data: %u bytes\n", spiffsSize);
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

