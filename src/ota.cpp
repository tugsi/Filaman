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
    static size_t spiffsSize = 0x30000;  // 192KB SPIFFS size
    static size_t spiffsStored = 0;
    
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

        isFullImage = (contentLength > 0x300000);  // Über 3MB ist ein full image
        
        if (isFullImage) {
            // Alloziere Buffer für SPIFFS-Daten
            spiffsBuffer = (uint8_t*)malloc(spiffsSize);
            if (!spiffsBuffer) {
                Serial.println("Failed to allocate SPIFFS buffer");
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
                return;
            }
            memset(spiffsBuffer, 0xFF, spiffsSize);
            spiffsStored = 0;
            
            // Starte Update mit der Firmware-Größe
            if (!Update.begin(0x3D0000, U_FLASH)) {  // Nur bis zum SPIFFS-Start
                Serial.println("Could not begin full image update");
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Could not start full update\"}");
                return;
            }
        } else {
            // Normales Firmware-Update
            if (!Update.begin(contentLength)) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                return;
            }
        }
        
        currentOffset = 0;
        Serial.printf("%s update started\n", isFullImage ? "Full image" : "Firmware");
    }

    if (isFullImage && currentOffset >= 0x3D0000) {
        // SPIFFS-Daten sammeln
        size_t spiffsOffset = currentOffset - 0x3D0000;
        if (spiffsOffset < spiffsSize) {
            size_t copyLen = min(len, spiffsSize - spiffsOffset);
            memcpy(spiffsBuffer + spiffsOffset, data, copyLen);
            spiffsStored += copyLen;
            Serial.printf("Stored SPIFFS data: offset=0x%X, len=%u\n", spiffsOffset, copyLen);
        }
        
        // Nur die Firmware-Daten an Update.write() übergeben
        return;
    }

    // Schreibe Firmware-Daten
    size_t writeLen = isFullImage ? min(len, 0x3D0000 - currentOffset) : len;
    if (writeLen > 0) {
        if (Update.write(data, writeLen) != writeLen) {
            String errorMsg = Update.errorString();
            if (errorMsg != "No Error") {
                Update.printError(Serial);
                if (spiffsBuffer) {
                    free(spiffsBuffer);
                    spiffsBuffer = nullptr;
                }
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Error writing update: " + errorMsg + "\"}");
                return;
            }
        }
    }
    
    currentOffset += len;
    
    // Progress logging
    if ((currentOffset % 0x40000) == 0) {
        Serial.printf("Update progress: 0x%X of 0x%X bytes (%.1f%%)\n", 
            currentOffset, contentLength, (currentOffset * 100.0) / contentLength);
    }
    
    if (final) {
        bool success = true;
        
        // Beende Firmware-Update
        if (!Update.end(true)) {
            success = false;
            String errorMsg = Update.errorString();
            if (errorMsg != "No Error") {
                Update.printError(Serial);
                if (spiffsBuffer) {
                    free(spiffsBuffer);
                    spiffsBuffer = nullptr;
                }
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Firmware update failed: " + errorMsg + "\"}");
                return;
            }
        }
        
        // SPIFFS aktualisieren wenn es ein full image war
        if (success && isFullImage && spiffsBuffer && spiffsStored > 0) {
            Serial.printf("Starting SPIFFS update (size: 0x%X)\n", spiffsStored);
            
            if (SPIFFS.begin(true)) {
                SPIFFS.end();  // Unmount first
            }
            
            if (!Update.begin(spiffsSize, U_SPIFFS)) {
                Serial.println("Could not begin SPIFFS update");
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS update failed to start\"}");
                return;
            }
            
            if (Update.write(spiffsBuffer, spiffsSize) != spiffsSize) {
                Serial.println("SPIFFS Write Failed");
                Update.printError(Serial);
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS write failed\"}");
                return;
            }
            
            if (!Update.end(true)) {
                Serial.println("SPIFFS End Failed");
                Update.printError(Serial);
                free(spiffsBuffer);
                spiffsBuffer = nullptr;
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS finish failed\"}");
                return;
            }
            
            // Cleanup
            free(spiffsBuffer);
            spiffsBuffer = nullptr;
            
            // Verify SPIFFS
            if (!SPIFFS.begin(true)) {
                Serial.println("SPIFFS mount after update failed");
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SPIFFS verification failed\"}");
                return;
            }
            SPIFFS.end();
        }
        
        // Alles erfolgreich
        Serial.printf("Update complete: 0x%X bytes written\n", currentOffset);
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
        delay(500);
        ESP.restart();
    }
}

