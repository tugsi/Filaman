#include <Arduino.h>
#include "wlan.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>
#include "display.h"
#include "config.h"

WiFiManager wm;
bool wm_nonblocking = false;

void initWiFi() {
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    WiFi.setSleep(false); // disable sleep mode
  
    //esp_wifi_set_max_tx_power(72); // Setze maximale Sendeleistung auf 20dBm
  
    if(wm_nonblocking) wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(320); // Portal nach 5min schließen
  
    oledShowTopRow();
    oledShowMessage("WiFi Setup");
    
    bool res;
    // res = wm.autoConnect(); // auto generated AP name from chipid
    res = wm.autoConnect("FilaMan"); // anonymous ap
    // res = wm.autoConnect("spoolman","password"); // password protected ap
  
    if(!res) {
      Serial.println("Failed to connect or hit timeout");
      // ESP.restart();
      oledShowTopRow();
      oledShowMessage("WiFi not connected Check Portal");
    } 
    else {
      wifiOn = true;
  
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
  
      oledShowTopRow();
      display.display();
    }
  }