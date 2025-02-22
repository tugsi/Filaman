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

    /*
    // Neue API-Route für das Abrufen der Spool-Daten
    server.on("/api/spools", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("API-Aufruf: /api/spools");
        JsonDocument spoolsData = fetchSpoolsForWebsite();
        String response;
        serializeJson(spoolsData, response);
        request->send(200, "application/json", response);
    });
    */

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
        if (loadJsonValue("/bambu_credentials.json", doc) && doc["bambu_ip"].is<String>()) {
            String bambuIp = doc["bambu_ip"].as<String>();
            String bambuSerial = doc["bambu_serialnr"].as<String>();
            String bambuCode = doc["bambu_accesscode"].as<String>();
            bambuIp.trim();
            bambuSerial.trim();
            bambuCode.trim();

            html.replace("{{bambuIp}}", bambuIp ? bambuIp : "");            
            html.replace("{{bambuSerial}}", bambuSerial ? bambuSerial : "");
            html.replace("{{bambuCode}}", bambuCode ? bambuCode : "");
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
        bambu_ip.trim();
        bambu_serialnr.trim();
        bambu_accesscode.trim();

        if (bambu_ip.length() == 0 || bambu_serialnr.length() == 0 || bambu_accesscode.length() == 0) {
            request->send(400, "application/json", "{\"success\": false, \"error\": \"Empty parameter\"}");
            return;
        }

        bool success = saveBambuCredentials(bambu_ip, bambu_serialnr, bambu_accesscode);

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

    // Update-Handler mit verbesserter Fehlerbehandlung
    server.on("/update", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            // Nach Update-Abschluss
            bool success = !Update.hasError();
            
            // Bei SPIFFS Update und Erfolg: Restore Configs vor dem Neustart
            if (success && currentUpdateCommand == U_SPIFFS) {
                restoreJsonConfigs();
            }
            
            String message = success ? "Update successful" : String("Update failed: ") + Update.errorString();
            AsyncWebServerResponse *response = request->beginResponse(
                success ? 200 : 400,
                "application/json",
                "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            
            if (success) {
                oledShowMessage("Upgrade successful Rebooting");
                delay(500);
                ESP.restart();
            }
            else {
                oledShowMessage("Upgrade failed");
            }
        },
        [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            static size_t updateSize = 0;

            if (!index) {
                updateSize = request->contentLength();
                currentUpdateCommand = (filename.indexOf("website") > -1) ? U_SPIFFS : U_FLASH;
                
                if (currentUpdateCommand == U_SPIFFS) {
                    oledShowMessage("SPIFFS Update...");
                    backupJsonConfigs();
                    
                    // Get the actual SPIFFS partition size from ESP32
                    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
                    if (!partition) {
                        String errorMsg = "SPIFFS partition not found";
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"" + errorMsg + "\"}");
                        return;
                    }
                    
                    if (!Update.begin(partition->size, currentUpdateCommand)) {
                        String errorMsg = String("Update begin failed: ") + Update.errorString();
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"" + errorMsg + "\"}");
                        return;
                    }
                } else {
                    oledShowMessage("Firmware Update...");
                    if (!Update.begin(updateSize, currentUpdateCommand)) {
                        String errorMsg = String("Update begin failed: ") + Update.errorString();
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"" + errorMsg + "\"}");
                        return;
                    }
                }
            }

            if (len) {
                if (Update.write(data, len) != len) {
                    String errorMsg = String("Write failed: ") + Update.errorString();
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"" + errorMsg + "\"}");
                    return;
                }
                
                // Update OLED Display alle 5% und Webseite bei jeder Änderung
                static int lastProgress = -1;
                int currentProgress = (index + len) * 100 / updateSize;
                if (currentProgress != lastProgress) {
                    // OLED nur alle 5% aktualisieren
                    if (currentProgress % 5 == 0) {
                        oledShowMessage(String(currentProgress) + "% complete");
                    }
                    // Webseite bei jeder Änderung aktualisieren
                    lastProgress = currentProgress;
                    ws.textAll("{\"type\":\"updateProgress\",\"progress\":" + String(currentProgress) + "}");
                }
            }

            if (final) {
                if (!Update.end(true)) {
                    String errorMsg = String("Update end failed: ") + Update.errorString();
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"" + errorMsg + "\"}");
                    return;
                }
                // Sende finale Progress-Nachricht
                ws.textAll("{\"type\":\"updateProgress\",\"progress\":100}");
            }
        }
    );

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
