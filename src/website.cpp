#include "website.h"
#include "commonFS.h"
#include "api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "bambu.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include <Update.h>
#include "display.h"

#ifndef VERSION
  #define VERSION "1.1.0"
#endif

// Cache-Control Header definieren
#define CACHE_CONTROL "max-age=604800" // Cache für 1 Woche

AsyncWebServer server(webserverPort);
AsyncWebSocket ws("/ws");

uint8_t lastSuccess = 0;
uint8_t lastHasReadRfidTag = 0;

// Globale Variablen für Config Backups hinzufügen
String bambuCredentialsBackup;
String spoolmanUrlBackup;

// Globale Variable für den Update-Typ
static int currentUpdateCommand = 0;

// Globale Update-Variablen
static size_t updateTotalSize = 0;
static size_t updateWritten = 0;
static bool isSpiffsUpdate = false;

void sendUpdateProgress(int progress, const char* status = nullptr, const char* message = nullptr) {
    static int lastSentProgress = -1;
    
    // Verhindere zu häufige Updates
    if (progress == lastSentProgress && !status && !message) {
        return;
    }
    
    String progressMsg = "{\"type\":\"updateProgress\",\"progress\":" + String(progress);
    if (status) {
        progressMsg += ",\"status\":\"" + String(status) + "\"";
    }
    if (message) {
        progressMsg += ",\"message\":\"" + String(message) + "\"";
    }
    progressMsg += "}";
    
    // Sende die Nachricht mehrmals mit Verzögerung für wichtige Updates
    if (status || abs(progress - lastSentProgress) >= 10 || progress == 100) {
        for (int i = 0; i < 2; i++) {
            ws.textAll(progressMsg);
            delay(100);  // Längerer Delay zwischen Nachrichten
        }
    } else {
        ws.textAll(progressMsg);
        delay(50);
    }
    
    lastSentProgress = progress;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.println("Neuer Client verbunden!");
        // Sende die AMS-Daten an den neuen Client
        sendAmsData(client);
        sendNfcData(client);
        foundNfcTag(client, 0);
        sendWriteResult(client, 3);
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.println("Client getrennt.");
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("WebSocket Client #%u error(%u): %s\n", client->id(), *((uint16_t*)arg), (char*)data);
    } else if (type == WS_EVT_PONG) {
        Serial.printf("WebSocket Client #%u pong\n", client->id());
    } else if (type == WS_EVT_DATA) {
        String message = String((char*)data);
        JsonDocument doc;
        deserializeJson(doc, message);
        
        if (doc["type"] == "heartbeat") {
            // Sende Heartbeat-Antwort
            ws.text(client->id(), "{"
                "\"type\":\"heartbeat\","
                "\"freeHeap\":" + String(ESP.getFreeHeap()/1024) + ","
                "\"bambu_connected\":" + String(bambu_connected) + ","
                "\"spoolman_connected\":" + String(spoolman_connected) + ""
                "}");
        }

        else if (doc["type"] == "writeNfcTag") {
            if (doc["payload"].is<String>()) {
                // Versuche NFC-Daten zu schreiben
                String payloadString;
                serializeJson(doc["payload"], payloadString);
                startWriteJsonToTag(payloadString.c_str());
            }
        }

        else if (doc["type"] == "scale") {
            uint8_t success = 0;
            if (doc["payload"] == "tare") {
                success = tareScale();
            }

            if (doc["payload"] == "calibrate") {
                success = calibrate_scale();
            }

            if (success) {
                ws.textAll("{\"type\":\"scale\",\"payload\":\"success\"}");
            } else {
                ws.textAll("{\"type\":\"scale\",\"payload\":\"error\"}");
            }
        }

        else if (doc["type"] == "reconnect") {
            if (doc["payload"] == "bambu") {
                bambu_restart();
            }

            if (doc["payload"] == "spoolman") {
                initSpoolman();
            }
        }

        else if (doc["type"] == "setBambuSpool") {
            Serial.println(doc["payload"].as<String>());
            setBambuSpool(doc["payload"]);
        }

        else {
            Serial.println("Unbekannter WebSocket-Typ: " + doc["type"].as<String>());
        }
    }
}

// Funktion zum Laden und Ersetzen des Headers in einer HTML-Datei
String loadHtmlWithHeader(const char* filename) {
    Serial.println("Lade HTML-Datei: " + String(filename));
    if (!SPIFFS.exists(filename)) {
        Serial.println("Fehler: Datei nicht gefunden!");
        return "Fehler: Datei nicht gefunden!";
    }

    File file = SPIFFS.open(filename, "r");
    String html = file.readString();
    file.close();

    return html;
}

void sendWriteResult(AsyncWebSocketClient *client, uint8_t success) {
    // Sende Erfolg/Misserfolg an alle Clients
    String response = "{\"type\":\"writeNfcTag\",\"success\":" + String(success ? "1" : "0") + "}";
    ws.textAll(response);
}

void foundNfcTag(AsyncWebSocketClient *client, uint8_t success) {
    if (success == lastSuccess) return;
    ws.textAll("{\"type\":\"nfcTag\", \"payload\":{\"found\": " + String(success) + "}}");
    sendNfcData(nullptr);
    lastSuccess = success;
}

void sendNfcData(AsyncWebSocketClient *client) {
    if (lastHasReadRfidTag == hasReadRfidTag) return;
    if (hasReadRfidTag == 0) {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{}}");
    }
    else if (hasReadRfidTag == 1) {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":" + nfcJsonData + "}");
    }
    else if (hasReadRfidTag == 2)
    {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Empty Tag or Data not readable\"}}");
    }
    else if (hasReadRfidTag == 3)
    {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Schreibe Tag...\"}}");
    }
    else if (hasReadRfidTag == 4)
    {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Error writing to Tag\"}}");
    }
    else if (hasReadRfidTag == 5)
    {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"info\":\"Tag erfolgreich geschrieben\"}}");
    }
    else 
    {
        ws.textAll("{\"type\":\"nfcData\", \"payload\":{\"error\":\"Something went wrong\"}}");
    }
    lastHasReadRfidTag = hasReadRfidTag;
}

void sendAmsData(AsyncWebSocketClient *client) {
    if (ams_count > 0) {
        ws.textAll("{\"type\":\"amsData\",\"payload\":" + amsJsonData + "}");
    }
}

void handleUpdate(AsyncWebServer &server) {
    AsyncCallbackWebHandler* updateHandler = new AsyncCallbackWebHandler();
    updateHandler->setUri("/update");
    updateHandler->setMethod(HTTP_POST);
    
    updateHandler->onUpload([](AsyncWebServerRequest *request, String filename,
                             size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            updateTotalSize = request->contentLength();
            updateWritten = 0;
            isSpiffsUpdate = (filename.indexOf("website") > -1);
            
            if (isSpiffsUpdate) {
                // Backup vor dem Update
                sendUpdateProgress(0, "backup", "Backing up configurations...");
                delay(200);
                backupJsonConfigs();
                delay(200);
                
                const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
                if (!partition || !Update.begin(partition->size, U_SPIFFS)) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Update initialization failed\"}");
                    return;
                }
                sendUpdateProgress(5, "starting", "Starting SPIFFS update...");
                delay(200);
            } else {
                if (!Update.begin(updateTotalSize)) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Update initialization failed\"}");
                    return;
                }
                sendUpdateProgress(0, "starting", "Starting firmware update...");
                delay(200);
            }
        }

        if (len) {
            if (Update.write(data, len) != len) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Write failed\"}");
                return;
            }
            
            updateWritten += len;
            int currentProgress;
            
            // Berechne den Fortschritt basierend auf dem Update-Typ
            if (isSpiffsUpdate) {
                // SPIFFS: 5-75% für Upload
                currentProgress = 5 + (updateWritten * 100) / updateTotalSize;
            } else {
                // Firmware: 0-100% für Upload
                currentProgress = 1 + (updateWritten * 100) / updateTotalSize;
            }
            
            static int lastProgress = -1;
            if (currentProgress != lastProgress && (currentProgress % 10 == 0 || final)) {
                sendUpdateProgress(currentProgress, "uploading");
                oledShowMessage("Update: " + String(currentProgress) + "%");
                delay(50);
                lastProgress = currentProgress;
            }
        }

        if (final) {
            if (Update.end(true)) {
                if (isSpiffsUpdate) {
                    restoreJsonConfigs();
                }
            } else {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Update finalization failed\"}");
            }
        }
    });

    updateHandler->onRequest([](AsyncWebServerRequest *request) {
        if (Update.hasError()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Update failed\"}");
            return;
        }

        // Erste 100% Nachricht
        ws.textAll("{\"type\":\"updateProgress\",\"progress\":100,\"status\":\"success\",\"message\":\"Update successful! Restarting device...\"}");
        delay(2000);  // Längerer Delay für die erste Nachricht
        
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", 
            "{\"success\":true,\"message\":\"Update successful! Restarting device...\"}");
        response->addHeader("Connection", "close");
        request->send(response);
        
        // Zweite 100% Nachricht zur Sicherheit
        ws.textAll("{\"type\":\"updateProgress\",\"progress\":100,\"status\":\"success\",\"message\":\"Update successful! Restarting device...\"}");
        delay(3000);  // Noch längerer Delay vor dem Neustart
        
        ESP.restart();
    });

    server.addHandler(updateHandler);
}

void setupWebserver(AsyncWebServer &server) {
    // Deaktiviere alle Debug-Ausgaben
    Serial.setDebugOutput(false);
    
    // WebSocket-Optimierungen
    ws.onEvent(onWsEvent);
    ws.enable(true);

    // Konfiguriere Server für große Uploads
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){});
    server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){});

    // Lade die Spoolman-URL beim Booten
    spoolmanUrl = loadSpoolmanUrl();
    Serial.print("Geladene Spoolman-URL: ");
    Serial.println(spoolmanUrl);

    // Route für about
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /about erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    // Route für Waage
    server.on("/waage", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /waage erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/waage.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    // Route für RFID
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /rfid erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/rfid.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("RFID-Seite gesendet");
    });

    server.on("/api/url", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("API-Aufruf: /api/url");
        String jsonResponse = "{\"spoolman_url\": \"" + String(spoolmanUrl) + "\"}";
        request->send(200, "application/json", jsonResponse);
    });

    // Route für WiFi
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /wifi erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/wifi.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
    });

    // Route für Spoolman Setting
    server.on("/spoolman", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /spoolman erhalten");
        String html = loadHtmlWithHeader("/spoolman.html");
        html.replace("{{spoolmanUrl}}", spoolmanUrl);

        JsonDocument doc;
        if (loadJsonValue("/bambu_credentials.json", doc) && doc["bambu_ip"].is<String>()) 
        {
            String bambuIp = doc["bambu_ip"].as<String>();
            String bambuSerial = doc["bambu_serialnr"].as<String>();
            String bambuCode = doc["bambu_accesscode"].as<String>();
            autoSendToBambu = doc["autoSendToBambu"].as<bool>();
            bambuIp.trim();
            bambuSerial.trim();
            bambuCode.trim();

            html.replace("{{bambuIp}}", bambuIp ? bambuIp : "");            
            html.replace("{{bambuSerial}}", bambuSerial ? bambuSerial : "");
            html.replace("{{bambuCode}}", bambuCode ? bambuCode : "");
            html.replace("{{autoSendToBambu}}", autoSendToBambu ? "checked" : "");
        }
        else
        {
            html.replace("{{bambuIp}}", "");
            html.replace("{{bambuSerial}}", "");
            html.replace("{{bambuCode}}", "");
            html.replace("{{autoSendToBambu}}", "");
        }

        request->send(200, "text/html", html);
    });

    // Route für das Überprüfen der Spoolman-Instanz
    server.on("/api/checkSpoolman", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("url")) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing URL parameter\"}");
            return;
        }

        String url = request->getParam("url")->value();
        url.trim();
        
        bool healthy = saveSpoolmanUrl(url);
        String jsonResponse = "{\"healthy\": " + String(healthy ? "true" : "false") + "}";

        request->send(200, "application/json", jsonResponse);
    });

    // Route für das Überprüfen der Spoolman-Instanz
    server.on("/api/bambu", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!request->hasParam("bambu_ip") || !request->hasParam("bambu_serialnr") || !request->hasParam("bambu_accesscode")) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Missing parameter\"}");
            return;
        }

        String bambu_ip = request->getParam("bambu_ip")->value();
        String bambu_serialnr = request->getParam("bambu_serialnr")->value();
        String bambu_accesscode = request->getParam("bambu_accesscode")->value();
        bool autoSend = (request->getParam("autoSend")->value() == "true") ? true : false;
        Serial.println(autoSend);
        bambu_ip.trim();
        bambu_serialnr.trim();
        bambu_accesscode.trim();

        if (bambu_ip.length() == 0 || bambu_serialnr.length() == 0 || bambu_accesscode.length() == 0) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Empty parameter\"}");
            return;
        }

        bool success = saveBambuCredentials(bambu_ip, bambu_serialnr, bambu_accesscode, autoSend);

        request->send(200, "application/json", "{\"healthy\": " + String(success ? "true" : "false") + "}");
    });

    // Route für das Überprüfen der Spoolman-Instanz
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
        ESP.restart();
    });

    // Route für das Laden der CSS-Datei
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Lade style.css");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/style.css.gz", "text/css");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("style.css gesendet");
    });

    // Route für das Logo
    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/logo.png.gz", "image/png");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("logo.png gesendet");
    });

    // Route für Favicon
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/favicon.ico", "image/x-icon");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("favicon.ico gesendet");
    });

    // Route für spool_in.png
    server.on("/spool_in.png", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/spool_in.png.gz", "image/png");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("spool_in.png gesendet");
    });

    // Route für JavaScript Dateien
    server.on("/spoolman.js", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /spoolman.js erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/spoolman.js.gz", "text/javascript");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("Spoolman.js gesendet");
    });

    server.on("/rfid.js", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /rfid.js erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS,"/rfid.js.gz", "text/javascript");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", CACHE_CONTROL);
        request->send(response);
        Serial.println("RFID.js gesendet");
    });

    // Vereinfachter Update-Handler
    server.on("/upgrade", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/upgrade.html.gz", "text/html");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });

    // Update-Handler registrieren
    handleUpdate(server);

    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        String fm_version = VERSION;
        String jsonResponse = "{\"version\": \""+ fm_version +"\"}";
        request->send(200, "application/json", jsonResponse);
    });

    // Fehlerbehandlung für nicht gefundene Seiten
    server.onNotFound([](AsyncWebServerRequest *request){
        Serial.print("404 - Nicht gefunden: ");
        Serial.println(request->url());
        request->send(404, "text/plain", "Seite nicht gefunden");
    });

    // WebSocket-Route
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    ws.enable(true);

    // Starte den Webserver
    server.begin();
    Serial.println("Webserver gestartet");
}


void backupJsonConfigs() {
    // Bambu Credentials backup
    if (SPIFFS.exists("/bambu_credentials.json")) {
        File file = SPIFFS.open("/bambu_credentials.json", "r");
        if (file) {
            bambuCredentialsBackup = file.readString();
            file.close();
            Serial.println("Bambu credentials backed up");
        }
    }

    // Spoolman URL backup
    if (SPIFFS.exists("/spoolman_url.json")) {
        File file = SPIFFS.open("/spoolman_url.json", "r");
        if (file) {
            spoolmanUrlBackup = file.readString();
            file.close();
            Serial.println("Spoolman URL backed up");
        }
    }
}

void restoreJsonConfigs() {
    // Restore Bambu credentials
    if (bambuCredentialsBackup.length() > 0) {
        File file = SPIFFS.open("/bambu_credentials.json", "w");
        if (file) {
            file.print(bambuCredentialsBackup);
            file.close();
            Serial.println("Bambu credentials restored");
        }
        bambuCredentialsBackup = ""; // Clear backup
    }

    // Restore Spoolman URL
    if (spoolmanUrlBackup.length() > 0) {
        File file = SPIFFS.open("/spoolman_url.json", "w");
        if (file) {
            file.print(spoolmanUrlBackup);
            file.close();
            Serial.println("Spoolman URL restored");
        }
        spoolmanUrlBackup = ""; // Clear backup
    }
}
