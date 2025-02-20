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
        Serial.printf("Update Start: %s\n", filename.c_str());
        if (request->contentLength() == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        // Da die full.bin jetzt das korrekte Magic Byte hat, 
        // können wir ein normales Update ohne spezielle Flags starten
        if (!Update.begin()) {
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

