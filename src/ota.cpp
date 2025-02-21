#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"
#include "bambu.h"
#include "scale.h"
#include "nfc.h"

#define UPLOAD_TIMEOUT_MS 60000  // 60 Sekunden Timeout für den gesamten Upload
#define CHUNK_RESPONSE_TIMEOUT_MS 10000  // 10 Sekunden Timeout pro Chunk
#define MAX_FAILED_CHUNKS 3  // Maximale Anzahl fehlgeschlagener Chunks bevor Abbruch
#define MAX_FILE_SIZE 4194304    // 4MB Limit

static bool tasksAreStopped = false;
static uint32_t lastChunkTime = 0;
static size_t failedChunks = 0;
static size_t expectedOffset = 0;
static size_t totalSize = 0;

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

void backupJsonConfigs() {
    const char* configs[] = {"/bambu_credentials.json", "/spoolman_url.json"};
    for (const char* config : configs) {
        if (SPIFFS.exists(config)) {
            String backupPath = String(config) + ".bak";
            SPIFFS.remove(backupPath);
            SPIFFS.rename(config, backupPath);
        }
    }
}

void restoreJsonConfigs() {
    const char* configs[] = {"/bambu_credentials.json", "/spoolman_url.json"};
    for (const char* config : configs) {
        String backupPath = String(config) + ".bak";
        if (SPIFFS.exists(backupPath)) {
            SPIFFS.remove(config);
            SPIFFS.rename(backupPath, config);
        }
    }
}

void performStageTwo() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Error: Could not mount SPIFFS for stage 2");
        return;
    }

    // Backup JSON configs before update
    backupJsonConfigs();

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

    // Restore JSON configs after update
    restoreJsonConfigs();

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
    static uint32_t uploadStartTime = 0;
    
    if (!index) {
        // Überprüfe Gesamtgröße im Header
        if (request->hasHeader("X-Total-Size")) {
            totalSize = request->header("X-Total-Size").toInt();
            if (totalSize > MAX_FILE_SIZE) {
                request->send(413, "application/json", 
                            "{\"status\":\"error\",\"message\":\"File too large\"}");
                return;
            }
        }

        uploadStartTime = millis();
        lastChunkTime = millis();
        expectedOffset = 0;
        failedChunks = 0;
        
        bool isSpiffsUpdate = filename.endsWith("_spiffs.bin");
        Serial.printf("Update Start: %s (type: %s)\n", filename.c_str(), isSpiffsUpdate ? "SPIFFS" : "OTA");
        Serial.printf("Total size: %u bytes\n", totalSize);
        
        // Überprüfe Header für Chunk-Informationen
        if (request->hasHeader("X-Chunk-Offset")) {
            String offsetStr = request->header("X-Chunk-Offset");
            expectedOffset = offsetStr.toInt();
        }

        if (request->contentLength() == 0) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid file size\"}");
            return;
        }

        if (!tasksAreStopped) {
            stopAllTasks();
            tasksAreStopped = true;
        }

        if (isSpiffsUpdate) {
            if (!SPIFFS.begin(true)) {
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Could not mount SPIFFS\"}");
                return;
            }
            
            if (!Update.begin(totalSize > 0 ? totalSize : request->contentLength(), U_SPIFFS)) {
                Update.printError(Serial);
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"SPIFFS update initialization failed\"}");
                return;
            }
        } else {
            stagingFile = SPIFFS.open("/firmware.bin", "w");
            if (!stagingFile) {
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Could not create staging file\"}");
                return;
            }
        }
    }

    // Chunk Validierung
    if (request->hasHeader("X-Chunk-Offset")) {
        size_t chunkOffset = request->header("X-Chunk-Offset").toInt();
        if (chunkOffset != expectedOffset) {
            failedChunks++;
            if (failedChunks >= MAX_FAILED_CHUNKS) {
                if (stagingFile) {
                    stagingFile.close();
                    SPIFFS.remove("/firmware.bin");
                }
                Update.abort();
                request->send(400, "application/json", 
                            "{\"status\":\"error\",\"message\":\"Too many failed chunks\"}");
                return;
            }
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Invalid chunk offset\"}");
            return;
        }
    }

    // Timeout Überprüfungen
    uint32_t currentTime = millis();
    if (currentTime - uploadStartTime > UPLOAD_TIMEOUT_MS) {
        if (stagingFile) {
            stagingFile.close();
            SPIFFS.remove("/firmware.bin");
        }
        Update.abort();
        request->send(408, "application/json", "{\"status\":\"error\",\"message\":\"Upload timeout\"}");
        return;
    }

    if (currentTime - lastChunkTime > CHUNK_RESPONSE_TIMEOUT_MS) {
        if (stagingFile) {
            stagingFile.close();
            SPIFFS.remove("/firmware.bin");
        }
        Update.abort();
        request->send(408, "application/json", "{\"status\":\"error\",\"message\":\"Chunk timeout\"}");
        return;
    }
    lastChunkTime = currentTime;

    if (stagingFile) {
        size_t written = stagingFile.write(data, len);
        if (written != len) {
            stagingFile.close();
            SPIFFS.remove("/firmware.bin");
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Write to SPIFFS failed\"}");
            return;
        }
    } else {
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
            request->send(400, "application/json", 
                        "{\"status\":\"error\",\"message\":\"Write failed\"}");
            return;
        }
    }

    expectedOffset += len;

    if (final) {
        if (stagingFile) {
            stagingFile.close();
            Serial.println("Stage 1 complete - firmware staged in SPIFFS");
            request->send(200, "application/json", 
                        "{\"status\":\"success\",\"message\":\"Update staged successfully! Starting stage 2...\"}");
            delay(100);
            performStageTwo();
        } else {
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

