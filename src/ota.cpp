#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"
#include "bambu.h"
#include "scale.h"
#include "nfc.h"

const uint8_t ESP_MAGIC = 0xE9;
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
    static bool isFullImage = false;
    
    if (!index) {
        contentLength = request->contentLength();
        Serial.printf("Update Start: %s (size: %u bytes)\n", filename.c_str(), contentLength);
        
        if (contentLength == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Bei full.bin muss das Magic Byte nicht geprüft werden
        isFullImage = (contentLength > 0x300000);
        if (!isFullImage && data[0] != ESP_MAGIC) {
            Serial.printf("Wrong magic byte: 0x%02X (expected 0x%02X)\n", data[0], ESP_MAGIC);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid firmware format\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        if (!Update.begin(contentLength, isFullImage ? U_FLASH : U_FLASH)) {
            Update.printError(Serial);
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"OTA could not begin\"}");
            return;
        }
        
        Serial.printf("Starting %s update\n", isFullImage ? "full" : "firmware");
    }

    // Debug output für die ersten paar Bytes
    if (index == 0) {
        Serial.printf("First bytes: ");
        for(size_t i = 0; i < min(16UL, len); i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"OTA write failed\"}");
        return;
    }

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

