#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"


extern Adafruit_SSD1306 display;
extern bool wifiOn;

void setupDisplay();
void oledclearline();
void oledcleardata();
int oled_center_h(String text);
int oled_center_v(String text);

void oledShowWeight(uint16_t weight);
void oledShowMessage(String message, uint8_t size = 2);
void oledShowTopRow();
void oledShowIcon(const char* icon);

#endif
