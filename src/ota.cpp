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
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update size: %u bytes\n", contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        // Erweiterte Größenprüfung für full.bin
        isFullImage = (contentLength >= 0x400000); // 4MB full image
        
        if (!isFullImage) {
            if (!Update.begin(contentLength)) {
                Serial.printf("Not enough space for firmware: %u required\n", contentLength);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not enough space available\"}");
                return;
            }
            Serial.println("Firmware update started");
        } else {
            // Sicherstellen, dass genügend Flash-Speicher verfügbar ist
            if(!Update.begin(0x400000, U_FLASH)) { // 4MB Gesamtgröße
                Serial.println("Could not begin full image update");
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Could not start full update\"}");
                return;
            }
            Serial.println("Full image update started");
        }
        currentOffset = 0;
    }

    // Zusätzliche Debug-Ausgaben
    if (isFullImage) {
        Serial.printf("Writing at offset: 0x%X, length: %u\n", currentOffset, len);
    }

    if (Update.write(data, len) != len) {
        String errorMsg = Update.errorString();
        if (errorMsg != "No Error") {
            Update.printError(Serial);
            Serial.printf("Error at offset: 0x%X\n", currentOffset);
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

