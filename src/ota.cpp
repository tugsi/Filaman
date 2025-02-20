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
    
    // Flash layout constants from partitions.csv
    static const uint32_t FLASH_SIZE = 0x400000;    // 4MB total
    static const uint32_t APP_SIZE = 0x1E0000;      // Size per app partition
    static const uint32_t SPIFFS_OFFSET = 0x3D0000; // SPIFFS start
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update size: %u bytes (0x%X)\n", contentLength, contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        isFullImage = (contentLength >= SPIFFS_OFFSET);
        
        if (!isFullImage) {
            // Regular firmware update must not exceed app partition size
            if (contentLength > APP_SIZE) {
                Serial.printf("Firmware too large: 0x%X > 0x%X\n", contentLength, APP_SIZE);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Firmware too large\"}");
                return;
            }
            
            if (!Update.begin(contentLength)) {
                Serial.printf("Not enough space for firmware: %u required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                return;
            }
            Serial.printf("Firmware update started (size: 0x%X)\n", contentLength);
        } else {
            // Full image update
            if (contentLength > FLASH_SIZE) {
                Serial.printf("Image too large: 0x%X > 0x%X\n", contentLength, FLASH_SIZE);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Image too large\"}");
                return;
            }
            
            if (!Update.begin(FLASH_SIZE, U_FLASH)) {
                Serial.println("Could not begin full image update");
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Could not start full update\"}");
                return;
            }
            Serial.printf("Full image update started (size: 0x%X)\n", contentLength);
        }
        currentOffset = 0;
    }

    if (Update.write(data, len) != len) {
        String errorMsg = Update.errorString();
        if (errorMsg != "No Error") {
            Update.printError(Serial);
            Serial.printf("Error at offset: 0x%X of 0x%X bytes\n", currentOffset, contentLength);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
            return;
        }
    }
    
    // Progress logging
    if ((currentOffset % 0x40000) == 0) { // Log every 256KB
        Serial.printf("Update progress: 0x%X of 0x%X bytes (%.1f%%)\n", 
            currentOffset, 
            contentLength, 
            (currentOffset * 100.0) / contentLength);
    }
    
    currentOffset += len;
    
    if (final) {
        if (Update.end(true)) {
            Serial.printf("Update complete: 0x%X bytes written\n", currentOffset);
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

