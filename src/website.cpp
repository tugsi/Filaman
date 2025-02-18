#include "website.h"
#include "commonFS.h"
#include "api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "bambu.h"
#include "nfc.h"
#include "scale.h"
#include "esp_task_wdt.h"
#include "ota.h"

// Cache-Control Header definieren
#define CACHE_CONTROL "max-age=31536000" // Cache für 1 Jahr

AsyncWebServer server(webserverPort);
AsyncWebSocket ws("/ws");

uint8_t lastSuccess = 0;
uint8_t lastHasReadRfidTag = 0;

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
            if (doc.containsKey("payload")) {
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
        ws.textAll("{\"type\":\"amsData\", \"payload\":" + amsJsonData + "}");
    }
}

void setupWebserver(AsyncWebServer &server) {
    // Lade die Spoolman-URL beim Booten
    spoolmanUrl = loadSpoolmanUrl();
    Serial.print("Geladene Spoolman-URL: ");
    Serial.println(spoolmanUrl);

    // Route für about
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für /about erhalten");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/about.html.gz", "text/html");
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
        if (loadJsonValue("/bambu_credentials.json", doc) && doc.containsKey("bambu_ip")) {
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

    // Route für OTA Updates
    server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Anfrage für /ota erhalten");
        
        String html = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Firmware Update</title>
                <link rel="stylesheet" type="text/css" href="/style.css">
                <style>
                    .progress-container {
                        width: 100%;
                        margin: 20px 0;
                        display: none;
                    }
                    .progress-bar {
                        width: 0%;
                        height: 20px;
                        background-color: #4CAF50;
                        text-align: center;
                        line-height: 20px;
                        color: white;
                    }
                    .status {
                        margin: 10px 0;
                        padding: 10px;
                        display: none;
                    }
                    .error { background-color: #ffebee; color: #c62828; }
                    .success { background-color: #e8f5e9; color: #2e7d32; }
                </style>
            </head>
            <body>
                <h2>Firmware Update</h2>
                <form id="updateForm" enctype='multipart/form-data'>
                    <input type='file' name='update' accept='.bin' required>
                    <input type='submit' value='Update Firmware'>
                </form>
                <div class="progress-container">
                    <div class="progress-bar">0%</div>
                </div>
                <div class="status"></div>
                <script>
                    document.getElementById('updateForm').addEventListener('submit', async (e) => {
                        e.preventDefault();
                        const form = e.target;
                        const file = form.update.files[0];
                        const formData = new FormData();
                        formData.append('update', file);

                        const progress = document.querySelector('.progress-bar');
                        const progressContainer = document.querySelector('.progress-container');
                        const status = document.querySelector('.status');
                        
                        progressContainer.style.display = 'block';
                        status.style.display = 'none';
                        status.className = 'status';
                        form.querySelector('input[type=submit]').disabled = true;

                        try {
                            const response = await fetch('/update', {
                                method: 'POST',
                                body: formData,
                                onUploadProgress: (e) => {
                                    const percent = (e.loaded / e.total) * 100;
                                    progress.style.width = percent + '%';
                                    progress.textContent = Math.round(percent) + '%';
                                }
                            });

                            const result = await response.text();
                            status.textContent = result;
                            status.classList.add(response.ok ? 'success' : 'error');
                            status.style.display = 'block';

                            if (response.ok) {
                                setTimeout(() => {
                                    window.location.reload();
                                }, 5000);
                            } else {
                                form.querySelector('input[type=submit]').disabled = false;
                            }
                        } catch (error) {
                            status.textContent = 'Update failed: ' + error.message;
                            status.classList.add('error');
                            status.style.display = 'block';
                            form.querySelector('input[type=submit]').disabled = false;
                        }
                    });
                </script>
            </body>
            </html>
        )";
        request->send(200, "text/html", html);
    });

    server.on("/update", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            // The response will be sent from handleOTAUpload when the upload is complete
        },
        handleOTAUpload
    );

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
