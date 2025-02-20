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
        bool isFullImage = filename.endsWith("full.bin");
        Serial.printf("Update Start: %s (type: %s)\n", filename.c_str(), isFullImage ? "full" : "OTA");
        
        if (request->contentLength() == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        bool success;
        if (isFullImage) {
            // Full image update ohne Magic Byte Check, aber mit U_FLASH
            success = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
        } else {
            // Normales OTA update mit Magic Byte Check
            if (data[0] != 0xE9) {
                Serial.printf("Wrong magic byte: 0x%02X (expected 0xE9)\n", data[0]);
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid firmware format\"}");
                return;
            }
            success = Update.begin(request->contentLength());
        }

        if (!success) {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Update start failed\"}");
            return;
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Write failed\"}");
        return;
    }

    if (final) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Update failed\"}");
            return;
        }
        Serial.println("Update successful, restarting...");
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
        delay(500);
        ESP.restart();
    }
}

