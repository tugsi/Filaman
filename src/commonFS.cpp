#include "commonFS.h"

bool saveJsonValue(const char* filename, const JsonDocument& doc) {
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        Serial.print("Fehler beim Öffnen der Datei zum Schreiben: ");
        Serial.println(filename);
        return false;
    }
    return true;
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

bool initializeSPIFFS() {
    // Erster Versuch
    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS mounted successfully.");
        return true;
    }
    
    // Formatierung versuchen
    Serial.println("Failed to mount SPIFFS. Formatting...");
    SPIFFS.format();
    
    // Zweiter Versuch nach Formatierung
    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS formatted and mounted successfully.");
        return true;
    }
    
    Serial.println("SPIFFS initialization failed completely.");
    return false;
}