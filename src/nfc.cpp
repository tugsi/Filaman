#include "nfc.h"
#include <Arduino.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>
#include "config.h"
#include "website.h"
#include "api.h"
#include "esp_task_wdt.h"
#include "scale.h"
#include <SPI.h>

// Pin definitions for both PN532 chips
#define PN532_SCK  (18)  // SPI SCK
#define PN532_MISO (19)  // SPI MISO
#define PN532_MOSI (23)  // SPI MOSI

// CS pins for each PN532
#define PN532_CS1  (5)   // CS for first PN532
#define PN532_CS2  (4)   // CS for second PN532

// Mifare authentication key
uint8_t keyA[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Create two PN532 instances
Adafruit_PN532 nfc1(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS1);
Adafruit_PN532 nfc2(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS2);

TaskHandle_t RfidReaderTask;

JsonDocument rfidData;
String spoolId = "";
String nfcJsonData = "";
volatile bool pauseBambuMqttTask = false;

volatile uint8_t hasReadRfidTag = 0;
// 0 = nicht gelesen
// 1 = erfolgreich gelesen
// 2 = fehler beim Lesen
// 3 = schreiben
// 4 = fehler beim Schreiben
// 5 = erfolgreich geschrieben
// 6 = reading
// ***** PN532

// Buffer for reading data
uint8_t data[32];

// Function to initialize a specific PN532
bool initPN532(Adafruit_PN532 &pn532) {
    pn532.begin();
    uint32_t versiondata = pn532.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("Didn't find PN532 board");
        return false;
    }
    
    // Got valid data, print it out!
    Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
    Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
    Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);
    
    // Configure board to read RFID tags
    pn532.SAMConfig();
    Serial.println("Waiting for an ISO14443A Card ...");
    return true;
}

void initNfc() {
    // Configure CS pins as outputs
    pinMode(PN532_CS1, OUTPUT);
    pinMode(PN532_CS2, OUTPUT);
    digitalWrite(PN532_CS1, HIGH);  // Deselect both chips initially
    digitalWrite(PN532_CS2, HIGH);

    // Initialize SPI
    SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI);
    SPI.setFrequency(1000000); // 1MHz SPI clock
    
    // Initialize both PN532 chips
    if (!initPN532(nfc1)) {
        Serial.println("Failed to initialize PN532 #1");
        return;
    }
    if (!initPN532(nfc2)) {
        Serial.println("Failed to initialize PN532 #2");
        return;
    }
    
    Serial.println("Both PN532 chips initialized successfully");
}

// Function to read a specific PN532
bool readPN532(Adafruit_PN532 &pn532, uint8_t *uid, uint8_t *uidLength) {
    uint8_t success;
    success = pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength);
    return success;
}

// Function to write to a specific PN532
bool writePN532(Adafruit_PN532 &pn532, uint8_t *uid, uint8_t uidLength, uint8_t *data, uint8_t dataLen) {
    if (!pn532.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 1, keyA)) {
        Serial.println("Failed to authenticate block");
        return false;
    }
    
    if (!pn532.mifareclassic_WriteDataBlock(4, data)) {
        Serial.println("Failed to write block");
        return false;
    }
    
    return true;
}

void loopNfc() {
    uint8_t uid1[] = {0, 0, 0, 0, 0, 0, 0};
    uint8_t uid2[] = {0, 0, 0, 0, 0, 0, 0};
    uint8_t uidLength1 = 0;
    uint8_t uidLength2 = 0;
    
    // Try to read from both PN532 chips
    bool success1 = readPN532(nfc1, uid1, &uidLength1);
    bool success2 = readPN532(nfc2, uid2, &uidLength2);
    
    if (success1 || success2) {
        // Process the first tag if found
        if (success1) {
            processTag(uid1, uidLength1, 1);
        }
        
        // Process the second tag if found
        if (success2) {
            processTag(uid2, uidLength2, 2);
        }
    }
}

void processTag(uint8_t *uid, uint8_t uidLength, uint8_t readerNumber) {
    // Create a unique identifier for this tag
    String tagId = "";
    for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) {
            tagId += "0";
        }
        tagId += String(uid[i], HEX);
        tagId += " ";
    }
    tagId.trim();
    
    // Select the appropriate PN532 based on reader number
    Adafruit_PN532 &pn532 = (readerNumber == 1) ? nfc1 : nfc2;
    
    // Read the tag data
    if (pn532.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 1, keyA)) {
        if (pn532.mifareclassic_ReadDataBlock(4, data)) {
            processNfcData(data, tagId);
        }
    }
}

void processNfcData(uint8_t *data, String tagId) {
    // Process the data and send it via WebSocket
    if (decodeNdefAndReturnJson(data)) {
        hasReadRfidTag = 1;
        sendNfcData(nullptr);
    } else {
        hasReadRfidTag = 2;
        oledShowMessage("NFC-Tag unknown");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// Function to write to a specific tag
bool writeNfcTag(uint8_t *uid, uint8_t uidLength, uint8_t *data, uint8_t dataLen, uint8_t readerNumber) {
    // Select the appropriate PN532 based on reader number
    Adafruit_PN532 &pn532 = (readerNumber == 1) ? nfc1 : nfc2;
    
    return writePN532(pn532, uid, uidLength, data, dataLen);
}

// ##### Funktionen für RFID #####
void payloadToJson(uint8_t *data) {
    const char* startJson = strchr((char*)data, '{');
    const char* endJson = strrchr((char*)data, '}');
  
    if (startJson && endJson && endJson > startJson) {
      String jsonString = String(startJson, endJson - startJson + 1);
      //Serial.print("Bereinigter JSON-String: ");
      //Serial.println(jsonString);
  
      // JSON-Dokument verarbeiten
      JsonDocument doc;  // Passen Sie die Größe an den JSON-Inhalt an
      DeserializationError error = deserializeJson(doc, jsonString);
  
      if (!error) {
        const char* color_hex = doc["color_hex"];
        const char* type = doc["type"];
        int min_temp = doc["min_temp"];
        int max_temp = doc["max_temp"];
        const char* brand = doc["brand"];
  
        Serial.println();
        Serial.println("-----------------");
        Serial.println("JSON-Parsed Data:");
        Serial.println(color_hex);
        Serial.println(type);
        Serial.println(min_temp);
        Serial.println(max_temp);
        Serial.println(brand);
        Serial.println("-----------------");
        Serial.println();
      } else {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
      }
    } else {
        Serial.println("Kein gültiger JSON-Inhalt gefunden oder fehlerhafte Formatierung.");
        //writeJsonToTag("{\"version\":\"1.0\",\"protocol\":\"NFC\",\"color_hex\":\"#FFFFFF\",\"type\":\"Example\",\"min_temp\":10,\"max_temp\":30,\"brand\":\"BrandName\"}");
    }
  }

bool formatNdefTag() {
    uint8_t ndefInit[] = { 0x03, 0x00, 0xFE }; // NDEF Initialisierungsnachricht
    bool success = true;
    int pageOffset = 4; // Startseite für NDEF-Daten auf NTAG2xx
  
    Serial.println();
    Serial.println("Formatiere NDEF-Tag...");
  
    // Schreibe die Initialisierungsnachricht auf die ersten Seiten
    for (int i = 0; i < sizeof(ndefInit); i += 4) {
      if (!nfc1.ntag2xx_WritePage(pageOffset + (i / 4), &ndefInit[i])) {
          success = false;
          break;
      }
    }
  
    return success;
  }

uint16_t readTagSize(Adafruit_PN532 &pn532) {
  uint8_t buffer[4];
  memset(buffer, 0, 4);
  pn532.ntag2xx_ReadPage(3, buffer);
  return buffer[2]*8;
}

uint8_t ntag2xx_WriteNDEF(const char *payload, Adafruit_PN532 &pn532) {
  uint16_t tagSize = readTagSize(pn532);
  Serial.print("Tag Size: ");Serial.println(tagSize);

  uint8_t pageBuffer[4] = {0, 0, 0, 0};
  Serial.println("Beginne mit dem Schreiben der NDEF-Nachricht...");
  
  // Figure out how long the string is
  uint8_t len = strlen(payload);
  Serial.print("Länge der Payload: ");
  Serial.println(len);
  
  Serial.print("Payload: ");Serial.println(payload);

  // Setup the record header
  // See NFCForum-TS-Type-2-Tag_1.1.pdf for details
  uint8_t pageHeader[21] = {
    /* NDEF Message TLV - JSON Record */
    0x03, /* Tag Field (0x03 = NDEF Message) */
    (uint8_t)(len+3+16), /* Payload Length (including NDEF header) */
    0xD2, /* NDEF Record Header (TNF=0x2:MIME Media + SR + ME + MB) */
    0x10, /* Type Length for the record type indicator */
    (uint8_t)(len), /* Payload len */
    'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'j', 's', 'o', 'n'
  };

  // Make sure the URI payload will fit in dataLen (include 0xFE trailer)
  if ((len < 1) || (len + 1 > (tagSize - sizeof(pageHeader)))) 
  {
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("Fehler: Die Nutzlast passt nicht in die Datenlänge.");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();
    return 0;
  }

  // Kombiniere Header und Payload
  int totalSize = sizeof(pageHeader) + len;
  uint8_t* combinedData = (uint8_t*) malloc(totalSize);
  if (combinedData == NULL) 
  {
    Serial.println("Fehler: Nicht genug Speicher vorhanden.");
    oledShowMessage("Tag too small");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    return 0;
  }

  // Kombiniere Header und Payload
  memcpy(combinedData, pageHeader, sizeof(pageHeader));
  memcpy(&combinedData[sizeof(pageHeader)], payload, len);

  // Schreibe die Seiten
  uint8_t a = 0;
  uint8_t i = 0;
  while (totalSize > 0) {
    memset(pageBuffer, 0, 4);
    int bytesToWrite = (totalSize < 4) ? totalSize : 4;
    memcpy(pageBuffer, combinedData + a, bytesToWrite);

    if (!(pn532.ntag2xx_WritePage(4+i, pageBuffer))) 
    {
      Serial.println("Fehler beim Schreiben der Seite.");
      free(combinedData);
      return 0;
    }

    yield();
    i++;
    a += 4;
    totalSize -= bytesToWrite;
  }

  // Ensure the NDEF message is properly terminated
  memset(pageBuffer, 0, 4);
  pageBuffer[0] = 0xFE; // NDEF record footer
  if (!(pn532.ntag2xx_WritePage(4+i, pageBuffer))) 
  {
    Serial.println("Fehler beim Schreiben des End-Bits.");
    free(combinedData);
    return 0;
  }

  Serial.println("NDEF-Nachricht erfolgreich geschrieben.");
  free(combinedData);
  return 1;
}

bool decodeNdefAndReturnJson(const byte* encodedMessage) {
  byte typeLength = encodedMessage[3];
  byte payloadLength = encodedMessage[4];

  nfcJsonData = "";

  for (int i = 2; i < payloadLength+2; i++) 
  {
    nfcJsonData += (char)encodedMessage[3 + typeLength + i];
  }

  // JSON-Dokument verarbeiten
  JsonDocument doc;  // Passen Sie die Größe an den JSON-Inhalt an
  DeserializationError error = deserializeJson(doc, nfcJsonData);
  if (error) 
  {
    nfcJsonData = "";
    Serial.println("Fehler beim Verarbeiten des JSON-Dokuments");
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return false;
  } 
  else 
  {
    // Sende die aktualisierten AMS-Daten an alle WebSocket-Clients
    Serial.println("JSON-Dokument erfolgreich verarbeitet");
    Serial.println(doc.as<String>());
    if (doc["sm_id"] != "") 
    {
      Serial.println("SPOOL-ID gefunden: " + doc["sm_id"].as<String>());
      spoolId = doc["sm_id"].as<String>();
    } 
    else 
    {
      Serial.println("Keine SPOOL-ID gefunden.");
      spoolId = "";
      oledShowMessage("Unknown Spool");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
  }

  return true;
}

void writeJsonToTag(void *parameter) {
  const char* payload = (const char*)parameter;

  Serial.println("Erstelle NDEF-Message...");
  Serial.println(payload);

  hasReadRfidTag = 3;
  vTaskSuspend(RfidReaderTask);
  vTaskDelay(50 / portTICK_PERIOD_MS);

  sendNfcData(nullptr);
  vTaskDelay(100 / portTICK_PERIOD_MS);
  oledShowMessage("Waiting for NFC-Tag");
  
  // Try both readers
  uint8_t success = 0;
  String uidString = "";
  Adafruit_PN532* activeReader = nullptr;
  
  for (uint16_t i = 0; i < 20; i++) {
    // Try first reader
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    success = nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 250);
    
    if (!success) {
      // Try second reader
      success = nfc2.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 250);
      if (success) {
        activeReader = &nfc2;
      }
    } else {
      activeReader = &nfc1;
    }

    if (success) {
      for (uint8_t i = 0; i < uidLength; i++) {
        uidString += String(uid[i], HEX);
        if (i < uidLength - 1) {
            uidString += ":";
        }
      }
      foundNfcTag(nullptr, success);
      break;
    }

    if (i == 0) oledShowMessage("Waiting for NFC-Tag");

    yield();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (success && activeReader != nullptr) {
    oledShowIcon("transfer");
    // Schreibe die NDEF-Message auf den Tag
    success = ntag2xx_WriteNDEF(payload, *activeReader);
    if (success) {
        Serial.println("NDEF-Message erfolgreich auf den Tag geschrieben");
        oledShowIcon("success");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        hasReadRfidTag = 5;
        sendNfcData(nullptr);
        pauseBambuMqttTask = false;
        
        if (updateSpoolTagId(uidString, payload)) {
          uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
          uint8_t uidLength;
          oledShowIcon("success");
          while (activeReader->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500)) {
            yield();
          }
        }
          
        vTaskResume(RfidReaderTask);
        vTaskDelay(500 / portTICK_PERIOD_MS);        
    } else {
        Serial.println("Fehler beim Schreiben der NDEF-Message auf den Tag");
        oledShowIcon("failed");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        hasReadRfidTag = 4;
    }
  } else {
    Serial.println("Fehler: Kein Tag zu schreiben gefunden.");
    oledShowMessage("No NFC-Tag found");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    hasReadRfidTag = 0;
  }
  
  sendWriteResult(nullptr, success);
  sendNfcData(nullptr);

  vTaskResume(RfidReaderTask);
  pauseBambuMqttTask = false;

  vTaskDelete(NULL);
}

void startWriteJsonToTag(const char* payload) {
  char* payloadCopy = strdup(payload);
  
  // Task nicht mehrfach starten
  if (hasReadRfidTag != 3) {
    // Erstelle die Task
    xTaskCreate(
        writeJsonToTag,        // Task-Funktion
        "WriteJsonToTagTask",       // Task-Name
        5115,                        // Stackgröße in Bytes
        (void*)payloadCopy,         // Parameter
        rfidWriteTaskPrio,           // Priorität
        NULL                         // Task-Handle (nicht benötigt)
    );
  }
}

void scanRfidTask(void * parameter) {
  Serial.println("RFID Task gestartet");
  for(;;) {
    if (hasReadRfidTag != 3) {
      yield();

      uint8_t success = 0;
      uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
      uint8_t uidLength;
      Adafruit_PN532* activeReader = nullptr;

      // Try first reader
      success = nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);
      if (success) {
        activeReader = &nfc1;
      } else {
        // Try second reader
        success = nfc2.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);
        if (success) {
          activeReader = &nfc2;
        }
      }

      foundNfcTag(nullptr, success);
      
      if (success && hasReadRfidTag != 1 && activeReader != nullptr) {
        Serial.println("Found an ISO14443A card");

        hasReadRfidTag = 6;
        oledShowIcon("transfer");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        if (uidLength == 7) {
          uint16_t tagSize = readTagSize(*activeReader);
          if(tagSize > 0) {
            uint8_t* data = (uint8_t*)malloc(tagSize);
            memset(data, 0, tagSize);

            Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");
            
            uint8_t numPages = readTagSize(*activeReader)/4;
            for (uint8_t i = 4; i < 4+numPages; i++) {
              if (!activeReader->ntag2xx_ReadPage(i, data+(i-4) * 4)) {
                break;
              }
              if (data[(i - 4) * 4] == 0xFE) {
                break;
              }

              yield();
              esp_task_wdt_reset();
              vTaskDelay(pdMS_TO_TICKS(1));
            }

            if (!decodeNdefAndReturnJson(data)) {
              oledShowMessage("NFC-Tag unknown");
              vTaskDelay(2000 / portTICK_PERIOD_MS);
              hasReadRfidTag = 2;
            } else {
              hasReadRfidTag = 1;
            }

            free(data);
          } else {
            oledShowMessage("NFC-Tag read error");
            hasReadRfidTag = 2;
          }
        } else {
          Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
        }
      }

      if (!success && hasReadRfidTag > 0) {
        hasReadRfidTag = 0;
        nfcJsonData = "";
        Serial.println("Tag entfernt");
        if (!autoSendToBambu) oledShowWeight(weight);
      }

      sendNfcData(nullptr);
    }
    yield();
  }
}

void startNfc() {
  initNfc();
  BaseType_t result = xTaskCreatePinnedToCore(
    scanRfidTask, /* Function to implement the task */
    "RfidReader", /* Name of the task */
    5115,  /* Stack size in words */
    NULL,  /* Task input parameter */
    rfidTaskPrio,  /* Priority of the task */
    &RfidReaderTask,  /* Task handle. */
    rfidTaskCore); /* Core where the task should run */

  if (result != pdPASS) {
    Serial.println("Fehler beim Erstellen des RFID Tasks");
  } else {
    Serial.println("RFID Task erfolgreich erstellt");
  }
}