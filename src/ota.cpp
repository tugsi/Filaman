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

void performStageTwo() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Error: Could not mount SPIFFS for stage 2");
        return;
    }

    File firmwareFile = SPIFFS.open("/firmware.bin", "r");
    if (!firmwareFile) {
        Serial.println("Error: Could not open firmware.bin from SPIFFS");
        return;
    }

    size_t firmwareSize = firmwareFile.size();
    size_t maxAppSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;

    Serial.printf("Stage 2 - Firmware size: %u bytes\n", firmwareSize);
    Serial.printf("Available space: %u bytes\n", maxAppSpace);

    if (firmwareSize > maxAppSpace) {
        Serial.printf("Error: Not enough space for firmware. Need %u bytes but only have %u bytes\n", 
                    firmwareSize, maxAppSpace);
        return;
    }

    if (!Update.begin(firmwareSize)) {
        Update.printError(Serial);
        return;
    }

    size_t written = Update.writeStream(firmwareFile);
    if (written != firmwareSize) {
        Update.printError(Serial);
        return;
    }

    if (!Update.end(true)) {
        Update.printError(Serial);
        return;
    }

    firmwareFile.close();
    SPIFFS.remove("/firmware.bin"); // Cleanup
    Serial.println("Stage 2 update successful, restarting...");
    delay(500);
    ESP.restart();
}

void checkForStagedUpdate() {
    if (!SPIFFS.begin(true)) {
        return;
    }

    if (SPIFFS.exists("/firmware.bin")) {
        Serial.println("Found staged firmware update, initiating stage 2...");
        performStageTwo();
    }
}

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File stagingFile;
    
    if (!index) {
        bool isSpiffsUpdate = filename.endsWith("_spiffs.bin");
        Serial.printf("Update Start: %s (type: %s)\n", filename.c_str(), isSpiffsUpdate ? "SPIFFS" : "OTA");
        
        if (request->contentLength() == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        // Stop tasks before update
        if (!tasksAreStopped && (RfidReaderTask || BambuMqttTask || ScaleTask)) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        size_t updateSize = request->contentLength();
        
        if (isSpiffsUpdate) {
            if (!SPIFFS.begin(true)) {
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Could not mount SPIFFS\"}");
                return;
            }
            
            // Start SPIFFS update
            if (!Update.begin(updateSize, U_SPIFFS)) {
                Update.printError(Serial);
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"SPIFFS update initialization failed\"}");
                return;
            }
        } else {
            // Regular OTA update
            stagingFile = SPIFFS.open("/firmware.bin", "w");
            if (!stagingFile) {
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Could not create staging file\"}");
                return;
            }
        }
    }

    if (stagingFile) {
        // Stage 1: Write to SPIFFS
        if (stagingFile.write(data, len) != len) {
            stagingFile.close();
            SPIFFS.remove("/firmware.bin");
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Write to SPIFFS failed\"}");
            return;
        }
    } else {
        // Direct SPIFFS update
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Write failed\"}");
            return;
        }
    }

    if (final) {
        if (stagingFile) {
            // Finish Stage 1
            stagingFile.close();
            Serial.println("Stage 1 complete - firmware staged in SPIFFS");
            request->send(200, "application/json", 
                        "{\"status\":\"success\",\"message\":\"Update staged successfully! Starting stage 2...\"}");
            performStageTwo();
        } else {
            // Finish direct SPIFFS update
            if (!Update.end(true)) {
                Update.printError(Serial);
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Update failed\"}");
                return;
            }
            Serial.println("SPIFFS update successful, restarting...");
            request->send(200, "application/json", 
                        "{\"status\":\"success\",\"message\":\"SPIFFS update successful! Device will restart...\",\"restart\":true}");
            delay(500);
            ESP.restart();
        }
    }
}

