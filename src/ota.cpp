#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"
#include "bambu.h"
#include "scale.h"
#include "nfc.h"

static bool tasksAreStopped = false;

void stopAllTasks() {
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
    if (!index) {
        bool isFullImage = filename.endsWith(".bin");
        Serial.printf("Update Start: %s (type: %s)\n", filename.c_str(), isFullImage ? "full" : "OTA");
        
        if (request->contentLength() == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Berechne verfügbaren Speicherplatz
        size_t updateSize = request->contentLength();
        size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        
        Serial.printf("Update size: %u bytes\n", updateSize);
        Serial.printf("Available space: %u bytes\n", maxSketchSpace);
        
        if (updateSize > maxSketchSpace) {
            Serial.printf("Error: Not enough space. Need %u bytes but only have %u bytes available\n", 
                        updateSize, maxSketchSpace);
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Not enough space for update\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        // Ensure SPIFFS is ended before update
        if (SPIFFS.begin()) {
            SPIFFS.end();
        }

        bool success;
        if (isFullImage) {
            success = Update.begin(updateSize, U_FLASH);
        } else {
            if (data[0] != 0xE9) {
                Serial.printf("Wrong magic byte: 0x%02X (expected 0xE9)\n", data[0]);
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Invalid firmware format\"}");
                return;
            }
            success = Update.begin(updateSize);
        }

        if (!success) {
            Update.printError(Serial);
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Update initialization failed\"}");
            return;
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(400, "application/json", 
                    "{\"status\":\"error\",\"message\":\"Write failed\"}");
        return;
    }

    if (final) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Update failed\"}");
            return;
        }
        Serial.println("Update successful, restarting...");
        request->send(200, "application/json", 
                    "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
        delay(500);
        ESP.restart();
    }
}

