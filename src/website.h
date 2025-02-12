#ifndef WEBSITE_H
#define WEBSITE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "commonFS.h"
#include "api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include "bambu.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"

extern String spoolmanUrl;
extern AsyncWebServer server;
extern AsyncWebSocket ws;

void setupWebserver(AsyncWebServer &server);
void sendAmsData(AsyncWebSocketClient *client);
void sendNfcData(AsyncWebSocketClient *client);
void foundNfcTag(AsyncWebSocketClient *client, uint8_t success);
void sendWriteResult(AsyncWebSocketClient *client, uint8_t success);

#endif
