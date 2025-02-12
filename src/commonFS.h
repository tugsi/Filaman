#ifndef COMMONFS_H
#define COMMONFS_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

bool saveJsonValue(const char* filename, const JsonDocument& doc);
bool loadJsonValue(const char* filename, JsonDocument& doc);
bool initializeSPIFFS();

#endif
