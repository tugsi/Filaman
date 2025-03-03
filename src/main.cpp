#include <Arduino.h>
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

bool mainTaskWasPaused = 0;
uint8_t scaleTareCounter = 0;

// ##### SETUP #####
void setup() {
  Serial.begin(115200);

  uint64_t chipid;

  chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
  Serial.printf("%08X\n", (uint32_t)chipid); //print Low 4bytes.

  // Initialize SPIFFS
  initializeFileSystem();

  // Start Display
  setupDisplay();

  // WiFiManager
  initWiFi();

  // Webserver
  setupWebserver(server);

  // Spoolman API
  // api.cpp
  initSpoolman();

  // Bambu MQTT
  setupMqtt();

  // NFC Reader
  startNfc();

  start_scale();

  // WDT initialisieren mit 10 Sekunden Timeout
  bool panic = true; // Wenn true, löst ein WDT-Timeout einen System-Panik aus
  esp_task_wdt_init(10, panic);

  // Aktuellen Task (loopTask) zum Watchdog hinzufügen
  esp_task_wdt_add(NULL);
}


/**
 * Safe interval check that handles millis() overflow
 * @param currentTime Current millis() value
 * @param lastTime Last recorded time
 * @param interval Desired interval in milliseconds
 * @return True if interval has elapsed
 */
bool intervalElapsed(unsigned long currentTime, unsigned long &lastTime, unsigned long interval) {
  if (currentTime - lastTime >= interval || currentTime < lastTime) {
    lastTime = currentTime;
    return true;
  }
  return false;
}

unsigned long lastWeightReadTime = 0;
const unsigned long weightReadInterval = 1000; // 1 second

unsigned long lastAutoSetBambuAmsTime = 0;
const unsigned long autoSetBambuAmsInterval = 1000; // 1 second
uint8_t autoAmsCounter = 0;

uint8_t weightSend = 0;
int16_t lastWeight = 0;

unsigned long lastWifiCheckTime = 0;
const unsigned long wifiCheckInterval = 60000; // Überprüfe alle 60 Sekunden (60000 ms)

// ##### PROGRAM START #####
void loop() {
  unsigned long currentMillis = millis();

  // Überprüfe regelmäßig die WLAN-Verbindung
  if (intervalElapsed(currentMillis, lastWifiCheckTime, wifiCheckInterval)) {
    checkWiFiConnection();
  }

  // Wenn Bambu auto set Spool aktiv
  if (autoSendToBambu && autoSetToBambuSpoolId > 0) {
    if (intervalElapsed(currentMillis, lastAutoSetBambuAmsTime, autoSetBambuAmsInterval)) 
    {
      if (hasReadRfidTag == 0)
      {
        lastAutoSetBambuAmsTime = currentMillis;
        oledShowMessage("Auto Set         " + String(autoSetBambuAmsCounter - autoAmsCounter) + "s");
        autoAmsCounter++;

        if (autoAmsCounter >= autoSetBambuAmsCounter) 
        {
          autoSetToBambuSpoolId = 0;
          autoAmsCounter = 0;
          oledShowWeight(weight);
        }
      }
      else
      {
        autoAmsCounter = 0;
      }
    }
  }

  // Wenn Waage nicht Kalibriert
  if (scaleCalibrated == 3) 
  {
    oledShowMessage("Scale not calibrated!");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    yield();
    esp_task_wdt_reset();
    
    return;
  } 

  // Ausgabe der Waage auf Display
  if(pauseMainTask == 0)
  {
    if (mainTaskWasPaused || (weight != lastWeight && hasReadRfidTag == 0 && (!autoSendToBambu || autoSetToBambuSpoolId == 0)))
    {
      (weight < 2) ? ((weight < -2) ? oledShowMessage("!! -0") : oledShowWeight(0)) : oledShowWeight(weight);
    }
    mainTaskWasPaused = false;
  }
  else
  {
    mainTaskWasPaused = true;
  }


  // Wenn Timer abgelaufen und nicht gerade ein RFID-Tag geschrieben wird
  if (currentMillis - lastWeightReadTime >= weightReadInterval && hasReadRfidTag < 3)
  {
    lastWeightReadTime = currentMillis;

    // Prüfen ob die Waage korrekt genullt ist
    if ((weight > 0 && weight < 5) || weight < 0)
    {
      if(scaleTareCounter < 5)
      {
        scaleTareCounter++;
      }
      else
      {
        scaleTareRequest = true;
        scaleTareCounter = 0;
      }
      
    }
    else
    {
      scaleTareCounter = 0;
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
      autoSetToBambuSpoolId = spoolId.toInt();

      if (octoEnabled) 
      {
        updateSpoolOcto(autoSetToBambuSpoolId);
      }
    }
    else
    {
      oledShowIcon("failed");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
  }
  
  esp_task_wdt_reset();
}
