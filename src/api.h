#ifndef API_H
#define API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h> // Include for AsyncWebServerRequest
#include "website.h"
#include "display.h"
#include <ArduinoJson.h>

extern bool spoolman_connected;
extern String spoolmanUrl;

bool checkSpoolmanInstance(const String& url);
bool saveSpoolmanUrl(const String& url);
String loadSpoolmanUrl(); // Neue Funktion zum Laden der URL
bool checkSpoolmanExtraFields(); // Neue Funktion zum Überprüfen der Extrafelder
JsonDocument fetchSingleSpoolInfo(int spoolId); // API-Funktion für die Webseite
bool updateSpoolTagId(String uidString, const char* payload); // Neue Funktion zum Aktualisieren eines Spools
uint8_t updateSpoolWeight(String spoolId, uint16_t weight); // Neue Funktion zum Aktualisieren des Gewichts
bool initSpoolman(); // Neue Funktion zum Initialisieren von Spoolman

#endif
