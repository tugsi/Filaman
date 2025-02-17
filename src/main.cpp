#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <WiFi.h>

#include "wlan.h"
#include "config.h"
#include "website.h"
#include "api.h"
#include "display.h"
#include "bambu.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include "commonFS.h"

// ##### SETUP #####
void setup() {
  Serial.begin(115200);

  // Initialize SPIFFS
  initializeSPIFFS();

  // Start Display
  setupDisplay();

  // WiFiManager
  initWiFi();

  // Webserver
  Serial.println("Starte Webserver");
  setupWebserver(server);

  // Spoolman API
  // api.cpp
  initSpoolman();

  // Bambu MQTT
  // bambu.cpp
  setupMqtt();

  // mDNS
  Serial.println("Starte MDNS");
  if (!MDNS.begin("filaman")) {   // Set the hostname to "esp32.local"
    Serial.println("Error setting up MDNS responder!");
    while(1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  
  startNfc();

  start_scale();
  
  // WDT initialisieren mit 10 Sekunden Timeout
  bool panic = true; // Wenn true, löst ein WDT-Timeout einen System-Panik aus
  esp_task_wdt_init(10, panic); 

  // Aktuellen Task (loopTask) zum Watchdog hinzufügen
  esp_task_wdt_add(NULL);

  // Optional: Andere Tasks zum Watchdog hinzufügen, falls nötig
  // esp_task_wdt_add(task_handle);
}


unsigned long lastWeightReadTime = 0;
const unsigned long weightReadInterval = 1000; // 1 second

unsigned long lastAmsSendTime = 0;
const unsigned long amsSendInterval = 60000; // 1 minute

uint8_t weightSend = 0;
int16_t lastWeight = 0;
uint8_t wifiErrorCounter = 0;

// ##### PROGRAM START #####
void loop() {
  // Überprüfe den WLAN-Status
  if (WiFi.status() != WL_CONNECTED) {
    wifiErrorCounter++;
    wifiOn = false;
  } else {
    wifiErrorCounter = 0;
    wifiOn = true;
  }
  if (wifiErrorCounter > 20) ESP.restart();

  unsigned long currentMillis = millis();

  // Send AMS Data min every Minute
  if (currentMillis - lastAmsSendTime >= amsSendInterval) {
    lastAmsSendTime = currentMillis;
    sendAmsData(nullptr);
  }

  // Ausgabe der Waage auf Display
  if (pauseMainTask == 0 && weight != lastWeight && hasReadRfidTag == 0)
  {
    (weight < 0) ? oledShowMessage("!! -1") : oledShowWeight(weight);
  }

  // Wenn Timer abgelaufen und nicht gerade ein RFID-Tag geschrieben wird
  if (currentMillis - lastWeightReadTime >= weightReadInterval && hasReadRfidTag < 3)
  {
    lastWeightReadTime = currentMillis;

    // Prüfen ob die Waage korrekt genullt ist
    if ((weight > 0 && weight < 5) || weight < 0)
    {
      scale_tare_counter++;
    }
    else
    {
      scale_tare_counter = 0;
    }

    // Prüfen ob das Gewicht gleich bleibt und dann senden
    if (weight == lastWeight && weight > 5)
    {
      weigthCouterToApi++;
    } 
    else 
    {
      weigthCouterToApi = 0;
      weightSend = 0;
    }
  }

  // reset weight counter after writing tag
  if (currentMillis - lastWeightReadTime >= weightReadInterval && hasReadRfidTag > 1)
  {
    weigthCouterToApi = 0;
  }
  
  lastWeight = weight;

  // Wenn ein Tag mit SM id erkannte wurde und der Waage Counter anspricht an SM Senden
  if (spoolId != "" && weigthCouterToApi > 3 && weightSend == 0 && hasReadRfidTag == 1) {
    oledShowIcon("loading");
    if (updateSpoolWeight(spoolId, weight)) 
    {
      oledShowIcon("success");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      weightSend = 1;
    }
    else
    {
      oledShowIcon("failed");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
  }

  yield();
  esp_task_wdt_reset();
}
