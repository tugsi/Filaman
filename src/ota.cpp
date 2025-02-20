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
    static size_t contentLength = 0;
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update Start: %s (size: %u bytes)\n", filename.c_str(), contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Stoppe alle Tasks vor dem Update
        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        // Für full.bin keine Magic Byte Prüfung
        bool isFullImage = (contentLength > 0x300000);
        if (!isFullImage && data[0] != 0xE9) {
            Serial.printf("Wrong magic byte: 0x%02X (expected 0xE9)\n", data[0]);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid firmware format\"}");
            return;
        }

        // Bei full.bin UPDATE_SIZE_UNKNOWN verwenden
        if (!Update.begin(isFullImage ? UPDATE_SIZE_UNKNOWN : contentLength)) {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"OTA could not begin\"}");
            return;
        }
        Serial.printf("Starting %s update\n", isFullImage ? "full" : "firmware");
    }

    // Schreibe Update-Daten
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"OTA write failed\"}");
        return;
    }

    // Update abschließen
    if (final) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"OTA end failed\"}");
            return;
        }
        Serial.println("Update successful, restarting...");
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Update successful! Device will restart...\",\"restart\":true}");
        delay(500);
        ESP.restart();
    }
}

