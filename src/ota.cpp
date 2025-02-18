#include <Arduino.h>
#include "ota.h"
#include <Update.h>
#include <SPIFFS.h>
#include "commonFS.h"
#include <esp_task_wdt.h>
#include <esp_int_wdt.h>
#include <esp_pthread.h>
#include <esp_ota_ops.h>

// Reduzierter Puffer für Dateioperationen
const size_t BUFFER_SIZE = 128;
const size_t MIN_FREE_HEAP = 32768; // 32KB minimum free heap

// Files to backup before update
const char* CONFIG_FILES[] = {
    "/bambu_credentials.json",
    "/spoolman_url.json"
};
const int CONFIG_FILES_COUNT = sizeof(CONFIG_FILES) / sizeof(CONFIG_FILES[0]);

bool backupConfigs() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return false;
    }

    for (int i = 0; i < CONFIG_FILES_COUNT; i++) {
        if (SPIFFS.exists(CONFIG_FILES[i])) {
            String backupFile = String(CONFIG_FILES[i]) + ".bak";
            
            if (SPIFFS.exists(backupFile)) {
                SPIFFS.remove(backupFile);
            }
            
            File sourceFile = SPIFFS.open(CONFIG_FILES[i], "r");
            File destFile = SPIFFS.open(backupFile, "w");
            
            if (!sourceFile || !destFile) {
                Serial.printf("Failed to open files for backup: %s\n", CONFIG_FILES[i]);
                return false;
            }

            // Verwenden Sie einen kleineren Puffer
            uint8_t* buf = (uint8_t*)malloc(BUFFER_SIZE);
            if (!buf) {
                Serial.println("Failed to allocate buffer");
                sourceFile.close();
                destFile.close();
                return false;
            }

            size_t len = 0;
            bool success = true;
            while ((len = sourceFile.read(buf, BUFFER_SIZE)) > 0) {
                if (destFile.write(buf, len) != len) {
                    Serial.println("Write failed");
                    success = false;
                    break;
                }
            }

            free(buf);
            sourceFile.close();
            destFile.close();

            if (!success) {
                return false;
            }
        }
    }
    return true;
}

bool restoreConfigs() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return false;
    }

    bool success = true;
    for (int i = 0; i < CONFIG_FILES_COUNT; i++) {
        String backupFile = String(CONFIG_FILES[i]) + ".bak";
        if (SPIFFS.exists(backupFile)) {
            if (SPIFFS.exists(CONFIG_FILES[i])) {
                SPIFFS.remove(CONFIG_FILES[i]);
            }
            
            File sourceFile = SPIFFS.open(backupFile, "r");
            File destFile = SPIFFS.open(CONFIG_FILES[i], "w");
            
            if (!sourceFile || !destFile) {
                Serial.printf("Failed to open files for restore: %s\n", CONFIG_FILES[i]);
                success = false;
                continue;
            }

            // Verwenden Sie einen kleineren Puffer
            uint8_t* buf = (uint8_t*)malloc(BUFFER_SIZE);
            if (!buf) {
                Serial.println("Failed to allocate buffer");
                sourceFile.close();
                destFile.close();
                success = false;
                continue;
            }

            size_t len = 0;
            while ((len = sourceFile.read(buf, BUFFER_SIZE)) > 0) {
                if (destFile.write(buf, len) != len) {
                    Serial.println("Write failed");
                    success = false;
                    break;
                }
            }

            free(buf);
            sourceFile.close();
            destFile.close();
            
            SPIFFS.remove(backupFile);
        }
    }
    return success;
}

void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static bool updateStarted = false;
    static size_t totalBytes = 0;

    if (!index) {
        updateStarted = false;
        totalBytes = 0;

        // Check minimum heap size
        if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
            request->send(500, "text/plain", "Not enough memory available");
            return;
        }

        size_t updateSize = request->contentLength();
        if (updateSize == 0) {
            request->send(400, "text/plain", "Invalid file size");
            return;
        }

        Serial.printf("Update size: %u bytes\n", updateSize);
        Serial.printf("Free space: %u bytes\n", ESP.getFreeSketchSpace());
        Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

        // Backup configs first
        if (!backupConfigs()) {
            request->send(500, "text/plain", "Failed to backup configuration");
            return;
        }

        // Close all files and unmount SPIFFS
        SPIFFS.end();

        // Begin update with minimal configuration
        if (!Update.begin(updateSize)) {
            Serial.printf("Update.begin failed: %s\n", Update.errorString());
            request->send(500, "text/plain", "Failed to start update");
            return;
        }

        updateStarted = true;
        Serial.println("Update process started");
    }

    if (!updateStarted) {
        request->send(500, "text/plain", "Update not properly started");
        return;
    }

    // Write update data
    if (Update.write(data, len) != len) {
        Serial.printf("Update.write failed: %s\n", Update.errorString());
        Update.abort();
        request->send(500, "text/plain", "Error during update");
        return;
    }

    totalBytes += len;
    if (totalBytes % (32 * 1024) == 0) {
        Serial.printf("Progress: %u bytes\n", totalBytes);
    }

    if (final) {
        if (!Update.end(true)) {
            Serial.printf("Update.end failed: %s\n", Update.errorString());
            request->send(500, "text/plain", "Update failed");
            return;
        }

        // Try to restore configs
        if (!SPIFFS.begin(true)) {
            Serial.println("Failed to mount SPIFFS for restore");
            request->send(200, "text/plain", "Update successful but config restore failed. Device will restart...");
            delay(2000);
            ESP.restart();
            return;
        }

        if (!restoreConfigs()) {
            Serial.println("Failed to restore configs");
        }

        request->send(200, "text/plain", "Update successful. Device will restart...");
        delay(2000);
        ESP.restart();
    }
}

