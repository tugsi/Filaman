#include "nfc.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "HX711.h"
#include <EEPROM.h>
#include "display.h"
#include "nfc.h"
#include "esp_task_wdt.h"

HX711 scale;

TaskHandle_t ScaleTask;

int16_t weight = 0;

uint8_t weigthCouterToApi = 0;
uint8_t scale_tare_counter = 0;
uint8_t pauseMainTask = 0;

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
      if (scale_tare_counter >= 5) 
      {
        scale.tare();
        scale_tare_counter = 0;
      }

      weight = round(scale.get_units());
    }
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Verzögerung, um die CPU nicht zu überlasten
  }
}

void start_scale() {
  Serial.println("Prüfe Calibration Value");
  long calibrationValue; // calibration value (see example file "Calibration.ino")
  //calibrationValue = 696.0; // uncomment this if you want to set the calibration value in the sketch

  EEPROM.begin(512);
  EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  //calibrationValue = EEPROM.read(calVal_eepromAdress);

  Serial.print("Read Scale Calibration Value ");
  Serial.println(calibrationValue);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  if (isnan(calibrationValue) || calibrationValue < 1) calibrationValue = defaultScaleCalibrationValue;

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
    10000,  /* Stack size in words */
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
  long newCalibrationValue;

  //vTaskSuspend(RfidReaderTask);
  vTaskDelete(RfidReaderTask);
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
    
    long newCalibrationValue = scale.get_units(10);
    Serial.print("Result: ");
    Serial.println(newCalibrationValue);

    newCalibrationValue = newCalibrationValue/SCALE_LEVEL_WEIGHT;

    if (newCalibrationValue > 0)
    {
      Serial.print("New calibration value has been set to: ");
      Serial.println(newCalibrationValue);
      Serial.print("Save this value to EEPROM adress ");
      Serial.println(calVal_eepromAdress);

      //EEPROM.put(calVal_eepromAdress, newCalibrationValue);
      EEPROM.put(calVal_eepromAdress, newCalibrationValue);
      EEPROM.commit();

      EEPROM.get(calVal_eepromAdress, newCalibrationValue);
      //newCalibrationValue = EEPROM.read(calVal_eepromAdress);

      Serial.print("Read Value ");
      Serial.println(newCalibrationValue);

      Serial.println("End calibration, revome weight");

      oledShowMessage("Remove weight");

      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }

      oledShowMessage("Calibration done");

      for (uint16_t i = 0; i < 2000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }

      //ESP.restart();
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
        return 0;
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
    return 0;
  }

  oledShowMessage("Scale Ready");

  
  Serial.println("starte Scale Task");
  start_scale();

  pauseBambuMqttTask = false;
  pauseMainTask = 0;

  return 1;
}
