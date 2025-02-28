#ifndef COMMONFS_H
#define COMMONFS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

bool saveJsonValue(const char* filename, const JsonDocument& doc);
bool loadJsonValue(const char* filename, JsonDocument& doc);
void initializeFileSystem();

#endif
