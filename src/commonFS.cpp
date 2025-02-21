#include "commonFS.h"
#include <SPIFFS.h>

bool saveJsonValue(const char* filename, const JsonDocument& doc) {
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        Serial.print("Fehler beim Öffnen der Datei zum Schreiben: ");
        Serial.println(filename);
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Fehler beim Serialisieren von JSON.");
        file.close();
        return false;
    }

    file.close();
    return true;
}

bool loadJsonValue(const char* filename, JsonDocument& doc) {
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.print("Fehler beim Öffnen der Datei zum Lesen: ");
        Serial.println(filename);
        return false;
    }
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.print("Fehler beim Deserialisieren von JSON: ");
        Serial.println(error.f_str());
        return false;
    }
    return true;
}

void initializeSPIFFS() {
    if (!SPIFFS.begin(true, "/spiffs", 10, "spiffs")) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    Serial.printf("SPIFFS Total: %u bytes\n", SPIFFS.totalBytes());
    Serial.printf("SPIFFS Used: %u bytes\n", SPIFFS.usedBytes());
    Serial.printf("SPIFFS Free: %u bytes\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
}