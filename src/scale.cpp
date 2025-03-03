#include "nfc.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "HX711.h"
#include "display.h"
#include "esp_task_wdt.h"
#include <Preferences.h>

HX711 scale;

TaskHandle_t ScaleTask;

int16_t weight = 0;

uint8_t weigthCouterToApi = 0;
bool scaleTareRequest = false;
uint8_t pauseMainTask = 0;
uint8_t scaleCalibrated = 1;

Preferences preferences;
const char* NVS_NAMESPACE = "scale";
const char* NVS_KEY_CALIBRATION = "cal_value";

// ##### Funktionen für Waage #####
uint8_t tareScale() {
  Serial.println("Tare scale");
  scale.tare();
  
  return 1;
}

void scale_loop(void * parameter) {
  Serial.println("++++++++++++++++++++++++++++++");
  Serial.println("Scale Loop started");
  Serial.println("++++++++++++++++++++++++++++++");

  for(;;) {
    if (scale.is_ready()) 
    {
      // Waage nochmal Taren, wenn zu lange Abweichung
      if (scaleTareRequest == true) 
      {
        Serial.println("Re-Tare scale");
        scale.tare();
        scaleTareRequest = false;
      }

      weight = round(scale.get_units());
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void start_scale() {
  Serial.println("Prüfe Calibration Value");
  float calibrationValue;

  // NVS lesen
  preferences.begin(NVS_NAMESPACE, true); // true = readonly
  calibrationValue = preferences.getFloat(NVS_KEY_CALIBRATION, defaultScaleCalibrationValue);
  preferences.end();

  Serial.print("Read Scale Calibration Value ");
  Serial.println(calibrationValue);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  if (isnan(calibrationValue) || calibrationValue < 1) {
    calibrationValue = defaultScaleCalibrationValue;
    scaleCalibrated = 0;

    oledShowMessage("Scale not calibrated!");
    for (uint16_t i = 0; i < 50000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
  }

  oledShowMessage("Scale Tare Please remove all");
  for (uint16_t i = 0; i < 2000; i++) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(1));
    esp_task_wdt_reset();
  }

  if (scale.wait_ready_timeout(1000))
  {
    scale.set_scale(calibrationValue); // this value is obtained by calibrating the scale with known weights; see the README for details
    scale.tare();
  }

  // Display Gewicht
  oledShowWeight(0);

  Serial.println("starte Scale Task");
  BaseType_t result = xTaskCreatePinnedToCore(
    scale_loop, /* Function to implement the task */
    "ScaleLoop", /* Name of the task */
    2048,  /* Stack size in words */
    NULL,  /* Task input parameter */
    scaleTaskPrio,  /* Priority of the task */
    &ScaleTask,  /* Task handle. */
    scaleTaskCore); /* Core where the task should run */

  if (result != pdPASS) {
      Serial.println("Fehler beim Erstellen des ScaleLoop-Tasks");
  } else {
      Serial.println("ScaleLoop-Task erfolgreich erstellt");
  }
}

uint8_t calibrate_scale() {
  uint8_t returnState = 0;
  float newCalibrationValue;

  vTaskSuspend(RfidReaderTask);
  vTaskSuspend(ScaleTask);

  pauseBambuMqttTask = true;
  pauseMainTask = 1;
  
  if (scale.wait_ready_timeout(1000))
  {
    
    scale.set_scale();
    oledShowMessage("Step 1 empty Scale");

    for (uint16_t i = 0; i < 5000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }

    scale.tare();
    Serial.println("Tare done...");
    Serial.print("Place a known weight on the scale...");

    oledShowMessage("Step 2 Place the weight");

    for (uint16_t i = 0; i < 5000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
    
    float newCalibrationValue = scale.get_units(10);
    Serial.print("Result: ");
    Serial.println(newCalibrationValue);

    newCalibrationValue = newCalibrationValue/SCALE_LEVEL_WEIGHT;

    if (newCalibrationValue > 0)
    {
      Serial.print("New calibration value has been set to: ");
      Serial.println(newCalibrationValue);

      // Speichern mit NVS
      preferences.begin(NVS_NAMESPACE, false); // false = readwrite
      preferences.putFloat(NVS_KEY_CALIBRATION, newCalibrationValue);
      preferences.end();

      // Verifizieren
      preferences.begin(NVS_NAMESPACE, true);
      float verifyValue = preferences.getFloat(NVS_KEY_CALIBRATION, 0);
      preferences.end();

      Serial.print("Verified stored value: ");
      Serial.println(verifyValue);

      Serial.println("End calibration, remove weight");

      oledShowMessage("Remove weight");

      scale.set_scale(newCalibrationValue);
      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }
      
      oledShowMessage("Scale calibrated");

      // For some reason it is not possible to re-tare the scale here, it will result in a wdt timeout. Instead let the scale loop do the taring
      //scale.tare();
      scaleTareRequest = true;

      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }

      returnState = 1;
    }
   
    else
    {
      {
        Serial.println("Calibration value is invalid. Please recalibrate.");

        oledShowMessage("Calibration ERROR Try again");

        for (uint16_t i = 0; i < 50000; i++) {
          yield();
          vTaskDelay(pdMS_TO_TICKS(1));
          esp_task_wdt_reset();
        }
        returnState = 0;
      }
    } 
  }
  else 
  {
    Serial.println("HX711 not found.");
    
    oledShowMessage("HX711 not found");

    for (uint16_t i = 0; i < 30000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
    returnState = 0;
  }

  vTaskResume(RfidReaderTask);
  vTaskResume(ScaleTask);
  pauseBambuMqttTask = false;
  pauseMainTask = 0;

  return returnState;
}
