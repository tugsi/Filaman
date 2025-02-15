#include "api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "commonFS.h"

bool spoolman_connected = false;
String spoolmanUrl = "";

struct SendToApiParams {
    String httpType;
    String spoolsUrl;
    String updatePayload;
};

/*
    // Spoolman Data
    {
        "version":"1.0",
        "protocol":"openspool",
        "color_hex":"AF7933",
        "type":"ABS",
        "min_temp":175,
        "max_temp":275,
        "brand":"Overture"
    }

    // FilaMan Data
    {
        "version":"1.0",
        "protocol":"openspool",
        "color_hex":"AF7933",
        "type":"ABS",
        "min_temp":175,
        "max_temp":275,
        "brand":"Overture",
        "sm_id": 
    }
*/

JsonDocument fetchSpoolsForWebsite() {
    HTTPClient http;
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool";

    Serial.print("Rufe Spool-Daten von: ");
    Serial.println(spoolsUrl);

    http.begin(spoolsUrl);
    int httpCode = http.GET();

    JsonDocument filteredDoc;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("Fehler beim Parsen der JSON-Antwort: ");
            Serial.println(error.c_str());
        } else {
            JsonArray spools = doc.as<JsonArray>();
            JsonArray filteredSpools = filteredDoc.to<JsonArray>();

            for (JsonObject spool : spools) {
                JsonObject filteredSpool = filteredSpools.createNestedObject();
                filteredSpool["extra"]["nfc_id"] = spool["extra"]["nfc_id"];

                JsonObject filament = filteredSpool.createNestedObject("filament");
                filament["sm_id"] = spool["id"];
                filament["id"] = spool["filament"]["id"];
                filament["name"] = spool["filament"]["name"];
                filament["material"] = spool["filament"]["material"];
                filament["color_hex"] = spool["filament"]["color_hex"];
                filament["nozzle_temperature"] = spool["filament"]["extra"]["nozzle_temperature"]; // [190,230]
                filament["price_meter"] = spool["filament"]["extra"]["price_meter"];
                filament["price_gramm"] = spool["filament"]["extra"]["price_gramm"];

                JsonObject vendor = filament.createNestedObject("vendor");
                vendor["id"] = spool["filament"]["vendor"]["id"];
                vendor["name"] = spool["filament"]["vendor"]["name"];
            }
        }
    } else {
        Serial.print("Fehler beim Abrufen der Spool-Daten. HTTP-Code: ");
        Serial.println(httpCode);
    }

    http.end();
    return filteredDoc;
}

JsonDocument fetchAllSpoolsInfo() {
    HTTPClient http;
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool";

    Serial.print("Rufe Spool-Daten von: ");
    Serial.println(spoolsUrl);

    http.begin(spoolsUrl);
    int httpCode = http.GET();

    JsonDocument filteredDoc;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("Fehler beim Parsen der JSON-Antwort: ");
            Serial.println(error.c_str());
        } else {
            JsonArray spools = doc.as<JsonArray>();
            JsonArray filteredSpools = filteredDoc.to<JsonArray>();

            for (JsonObject spool : spools) {
                JsonObject filteredSpool = filteredSpools.createNestedObject();
                filteredSpool["price"] = spool["price"];
                filteredSpool["remaining_weight"] = spool["remaining_weight"];
                filteredSpool["used_weight"] = spool["used_weight"];
                filteredSpool["extra"]["nfc_id"] = spool["extra"]["nfc_id"];

                JsonObject filament = filteredSpool.createNestedObject("filament");
                filament["id"] = spool["filament"]["id"];
                filament["name"] = spool["filament"]["name"];
                filament["material"] = spool["filament"]["material"];
                filament["density"] = spool["filament"]["density"];
                filament["diameter"] = spool["filament"]["diameter"];
                filament["spool_weight"] = spool["filament"]["spool_weight"];
                filament["color_hex"] = spool["filament"]["color_hex"];

                JsonObject vendor = filament.createNestedObject("vendor");
                vendor["id"] = spool["filament"]["vendor"]["id"];
                vendor["name"] = spool["filament"]["vendor"]["name"];

                JsonObject extra = filament.createNestedObject("extra");
                extra["nozzle_temperature"] = spool["filament"]["extra"]["nozzle_temperature"];
                extra["price_gramm"] = spool["filament"]["extra"]["price_gramm"];
                extra["price_meter"] = spool["filament"]["extra"]["price_meter"];
            }
        }
    } else {
        Serial.print("Fehler beim Abrufen der Spool-Daten. HTTP-Code: ");
        Serial.println(httpCode);
    }

    http.end();
    return filteredDoc;
}

void sendToApi(void *parameter) {
    SendToApiParams* params = (SendToApiParams*)parameter;

    // Extrahiere die Werte
    String httpType = params->httpType;
    String spoolsUrl = params->spoolsUrl;
    String updatePayload = params->updatePayload;
    

    HTTPClient http;
    http.begin(spoolsUrl);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.PUT(updatePayload);
    if (httpType == "PATCH") httpCode = http.PATCH(updatePayload);

    if (httpCode == HTTP_CODE_OK) {
        Serial.println("Gewicht der Spule erfolgreich aktualisiert");
    } else {
        Serial.println("Fehler beim Aktualisieren des Gewichts der Spule");
        oledShowMessage("Spoolman update failed");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    http.end();

    // Speicher freigeben
    delete params;
    vTaskDelete(NULL);
}

uint8_t updateSpoolTagId(String uidString, const char* payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.print("Fehler beim JSON-Parsing: ");
        Serial.println(error.c_str());
        return 0;
    }
    
    // Überprüfe, ob die erforderlichen Felder vorhanden sind
    if (!doc.containsKey("sm_id") || doc["sm_id"] == "") {
        Serial.println("Keine Spoolman-ID gefunden.");
        return 0;
    }

    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + doc["sm_id"].as<String>();
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);
    
    // Update Payload erstellen
    JsonDocument updateDoc;
    updateDoc["extra"]["nfc_id"] = "\""+uidString+"\"";
    
    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->httpType = "PATCH";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;

    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        4096,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );

    return 1;
}

uint8_t updateSpoolWeight(String spoolId, uint16_t weight) {
    String spoolsUrl = spoolmanUrl + apiUrl + "/spool/" + spoolId + "/measure";
    Serial.print("Update Spule mit URL: ");
    Serial.println(spoolsUrl);

    // Update Payload erstellen
    JsonDocument updateDoc;
    updateDoc["weight"] = weight;
    
    String updatePayload;
    serializeJson(updateDoc, updatePayload);
    Serial.print("Update Payload: ");
    Serial.println(updatePayload);

    SendToApiParams* params = new SendToApiParams();
    if (params == nullptr) {
        Serial.println("Fehler: Kann Speicher für Task-Parameter nicht allokieren.");
        return 0;
    }
    params->httpType = "PUT";
    params->spoolsUrl = spoolsUrl;
    params->updatePayload = updatePayload;

    // Erstelle die Task
    BaseType_t result = xTaskCreate(
        sendToApi,                // Task-Funktion
        "SendToApiTask",          // Task-Name
        4096,                     // Stackgröße in Bytes
        (void*)params,            // Parameter
        0,                        // Priorität
        NULL                      // Task-Handle (nicht benötigt)
    );

    return 1;
}

// #### Spoolman init
bool checkSpoolmanExtraFields() {
    HTTPClient http;
    String checkUrls[] = {
        spoolmanUrl + apiUrl + "/field/spool",
        spoolmanUrl + apiUrl + "/field/filament"
    };

    String spoolExtra[] = {
        "nfc_id"
    };

    String filamentExtra[] = {
        "nozzle_temperature",
        "price_meter",
        "price_gramm",
        "bambu_setting_id",
        "bambu_cali_id",
        "bambu_idx",
        "bambu_k",
        "bambu_flow_ratio",
        "bambu_max_volspeed"
    };

    String spoolExtraFields[] = {
        "{\"name\": \"NFC ID\","
        "\"key\": \"nfc_id\","
        "\"field_type\": \"text\"}"
    };

    String filamentExtraFields[] = {
        "{\"name\": \"Nozzle Temp\","
        "\"unit\": \"°C\","
        "\"field_type\": \"integer_range\","
        "\"default_value\": \"[190,230]\","
        "\"key\": \"nozzle_temperature\"}",

        "{\"name\": \"Price/m\","
        "\"unit\": \"€\","
        "\"field_type\": \"float\","
        "\"key\": \"price_meter\"}",
        
        "{\"name\": \"Price/g\","
        "\"unit\": \"€\","
        "\"field_type\": \"float\","
        "\"key\": \"price_gramm\"}",

        "{\"name\": \"Bambu Setting ID\","
        "\"field_type\": \"text\","
        "\"key\": \"bambu_setting_id\"}",

        "{\"name\": \"Bambu Cali ID\","
        "\"field_type\": \"text\","
        "\"key\": \"bambu_cali_id\"}",

        "{\"name\": \"Bambu Filament IDX\","
        "\"field_type\": \"text\","
        "\"key\": \"bambu_idx\"}",

        "{\"name\": \"Bambu k\","
        "\"field_type\": \"float\","
        "\"key\": \"bambu_k\"}",

        "{\"name\": \"Bambu Flow Ratio\","
        "\"field_type\": \"float\","
        "\"key\": \"bambu_flow_ratio\"}",

        "{\"name\": \"Bambu Max Vol. Speed\","
        "\"unit\": \"mm3/s\","
        "\"field_type\": \"integer\","
        "\"default_value\": \"12\","
        "\"key\": \"bambu_max_volspeed\"}"
    };

    Serial.println("Überprüfe Extrafelder...");

    int urlLength = sizeof(checkUrls) / sizeof(checkUrls[0]);

    for (uint8_t i = 0; i < urlLength; i++) {
        Serial.println();
        Serial.println("-------- Prüfe Felder für "+checkUrls[i]+" --------");
        http.begin(checkUrls[i]);
        int httpCode = http.GET();
    
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                String* extraFields;
                String* extraFieldData;
                u16_t extraLength;

                if (i == 0) {
                    extraFields = spoolExtra;
                    extraFieldData = spoolExtraFields;
                    extraLength = sizeof(spoolExtra) / sizeof(spoolExtra[0]);
                } else {
                    extraFields = filamentExtra;
                    extraFieldData = filamentExtraFields;
                    extraLength = sizeof(filamentExtra) / sizeof(filamentExtra[0]);
                }

                for (uint8_t s = 0; s < extraLength; s++) {
                    bool found = false;
                    for (JsonObject field : doc.as<JsonArray>()) {
                        if (field.containsKey("key") && field["key"] == extraFields[s]) {
                            Serial.println("Feld gefunden: " + extraFields[s]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        Serial.println("Feld nicht gefunden: " + extraFields[s]);

                        // Extrafeld hinzufügen
                        http.begin(checkUrls[i] + "/" + extraFields[s]);
                        http.addHeader("Content-Type", "application/json");
                        int httpCode = http.POST(extraFieldData[s]);

                         if (httpCode > 0) {
                            // Antwortscode und -nachricht abrufen
                            String response = http.getString();
                            //Serial.println("HTTP-Code: " + String(httpCode));
                            //Serial.println("Antwort: " + response);
                            if (httpCode != HTTP_CODE_OK) {

                                return false;
                            }
                        } else {
                            // Fehler beim Senden der Anfrage
                            Serial.println("Fehler beim Senden der Anfrage: " + String(http.errorToString(httpCode)));
                            return false;
                        }
                        //http.end();
                    }
                    yield();
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
            }
        }
        http.end();  
    }
    
    Serial.println("-------- ENDE Prüfe Felder --------");
    Serial.println();

    return true;
}

bool checkSpoolmanInstance(const String& url) {
    HTTPClient http;
    String healthUrl = url + apiUrl + "/health";

    Serial.print("Überprüfe Spoolman-Instanz unter: ");
    Serial.println(healthUrl);

    http.begin(healthUrl);
    int httpCode = http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            oledShowMessage("Spoolman available");
            vTaskDelay(1000 / portTICK_PERIOD_MS);

            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error && doc.containsKey("status")) {
                const char* status = doc["status"];
                http.end();

                if (!checkSpoolmanExtraFields()) {
                    Serial.println("Fehler beim Überprüfen der Extrafelder.");

                    oledShowMessage("Spoolman Error creating Extrafields");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    
                    return false;
                }

                spoolman_connected = true;
                return strcmp(status, "healthy") == 0;
            }
        }
    }
    http.end();
    return false;
}

bool saveSpoolmanUrl(const String& url) {
    if (!checkSpoolmanInstance(url)) return false;

    JsonDocument doc;
    doc["url"] = url;
    Serial.print("Speichere URL in Datei: ");
    Serial.println(url);
    if (!saveJsonValue("/spoolman_url.json", doc)) {
        Serial.println("Fehler beim Speichern der Spoolman-URL.");
    }
    spoolmanUrl = url;

    return true;
}

String loadSpoolmanUrl() {
    JsonDocument doc;
    if (loadJsonValue("/spoolman_url.json", doc) && doc.containsKey("url")) {
        return doc["url"].as<String>();
    }
    Serial.println("Keine gültige Spoolman-URL gefunden.");
    return "";
}

bool initSpoolman() {
    spoolmanUrl = loadSpoolmanUrl();
    spoolmanUrl.trim();
    if (spoolmanUrl == "") {
        Serial.println("Keine Spoolman-URL gefunden.");
        return false;
    }

    bool success = checkSpoolmanInstance(spoolmanUrl);
    if (!success) {
        Serial.println("Spoolman nicht erreichbar.");
        return false;
    }

    oledShowTopRow();
    return true;
}