#include "display.h"
#include "api.h"
#include <vector>
#include "icons.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool wifiOn = false;

void setupDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Stoppe das Programm, wenn das Display nicht initialisiert werden kann
    }
    display.setTextColor(WHITE);
    display.clearDisplay();
    display.display();

    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    display.setTextColor(WHITE);
    display.display();
    delay(1000); // Pause for 2 seconds
    oledShowTopRow();
    delay(2000);
}

void oledclearline() {
    int x, y;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            display.drawPixel(x, y, BLACK);
        }
    }
    //display.display();
}

void oledcleardata() {
    int x, y;
    for (y = OLED_DATA_START; y < OLED_DATA_END; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            display.drawPixel(x, y, BLACK);
        }
    }
    //display.display();
}

int oled_center_h(String text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (SCREEN_WIDTH - w) / 2;
}

int oled_center_v(String text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, OLED_DATA_START, &x1, &y1, &w, &h);
    // Zentrierung nur im Datenbereich zwischen OLED_DATA_START und OLED_DATA_END
    return OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - h) / 2);
}

std::vector<String> splitTextIntoLines(String text, uint8_t textSize) {
    std::vector<String> lines;
    display.setTextSize(textSize);
    
    // Text in Wörter aufteilen
    std::vector<String> words;
    int pos = 0;
    while (pos < text.length()) {
        // Überspringe Leerzeichen am Anfang
        while (pos < text.length() && text[pos] == ' ') pos++;
        
        // Finde nächstes Leerzeichen
        int nextSpace = text.indexOf(' ', pos);
        if (nextSpace == -1) {
            // Letztes Wort
            if (pos < text.length()) {
                words.push_back(text.substring(pos));
            }
            break;
        }
        // Wort hinzufügen
        words.push_back(text.substring(pos, nextSpace));
        pos = nextSpace + 1;
    }
    
    // Wörter zu Zeilen zusammenfügen
    String currentLine = "";
    for (size_t i = 0; i < words.size(); i++) {
        String testLine = currentLine;
        if (currentLine.length() > 0) testLine += " ";
        testLine += words[i];
        
        // Prüfe ob diese Kombination auf die Zeile passt
        int16_t x1, y1;
        uint16_t lineWidth, h;
        display.getTextBounds(testLine, 0, OLED_DATA_START, &x1, &y1, &lineWidth, &h);
        
        if (lineWidth <= SCREEN_WIDTH) {
            // Passt noch in diese Zeile
            currentLine = testLine;
        } else {
            // Neue Zeile beginnen
            if (currentLine.length() > 0) {
                lines.push_back(currentLine);
                currentLine = words[i];
            } else {
                // Ein einzelnes Wort ist zu lang
                lines.push_back(words[i]);
            }
        }
    }
    
    // Letzte Zeile hinzufügen
    if (currentLine.length() > 0) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

void oledShowMultilineMessage(String message, uint8_t size) {
    std::vector<String> lines;
    int maxLines = 3;  // Maximale Anzahl Zeilen für size 2
    
    // Erste Prüfung mit aktueller Größe
    lines = splitTextIntoLines(message, size);
    
    // Wenn mehr als maxLines Zeilen, reduziere Textgröße
    if (lines.size() > maxLines && size > 1) {
        size = 1;
        lines = splitTextIntoLines(message, size);
    }
    
    // Ausgabe
    display.setTextSize(size);
    int lineHeight = size * 8;
    int totalHeight = lines.size() * lineHeight;
    int startY = OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - totalHeight) / 2);
    
    for (size_t i = 0; i < lines.size(); i++) {
        display.setCursor(oled_center_h(lines[i]), startY + (i * lineHeight));
        display.print(lines[i]);
    }
    
    display.display();
}

void oledShowMessage(String message, uint8_t size) {
    oledcleardata();
    display.setTextSize(size);
    display.setTextWrap(false);
    
    // Prüfe ob Text in eine Zeile passt
    int16_t x1, y1;
    uint16_t textWidth, h;
    display.getTextBounds(message, 0, 0, &x1, &y1, &textWidth, &h);
   
    // Text passt in eine Zeile?
    if (textWidth <= SCREEN_WIDTH) {
        display.setCursor(oled_center_h(message), oled_center_v(message));
        display.print(message);
        display.display();
    } else {
        oledShowMultilineMessage(message, size);
    }
}

void oledShowTopRow() {
    oledclearline();

    if (bambu_connected == 1) {
        display.drawBitmap(50, 0, bitmap_bambu_on , 16, 16, WHITE);
    } else {
        display.drawBitmap(50, 0, bitmap_off , 16, 16, WHITE);
    }

    if (spoolman_connected == 1) {
        display.drawBitmap(80, 0, bitmap_spoolman_on , 16, 16, WHITE);
    } else {
        display.drawBitmap(80, 0, bitmap_off , 16, 16, WHITE);
    }

    if (wifiOn == 1) {
        display.drawBitmap(107, 0, wifi_on , 16, 16, WHITE);
    } else {
        display.drawBitmap(107, 0, wifi_off , 16, 16, WHITE);
    }
    
    display.display();
}

void oledShowIcon(const char* icon) {
    oledcleardata();

    uint16_t iconSize = OLED_DATA_END-OLED_DATA_START;
    uint16_t iconStart = (SCREEN_WIDTH - iconSize) / 2;

    if (icon == "failed") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_failed , iconSize, iconSize, WHITE);
    }
    else if (icon == "success") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_success , iconSize, iconSize, WHITE);
    }
    else if (icon == "transfer") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_transfer , iconSize, iconSize, WHITE);
    }
    else if (icon == "loading") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_loading , iconSize, iconSize, WHITE);
    }

    display.display();
}

void oledShowWeight(uint16_t weight) {
    // Display Gewicht
    oledcleardata();
    display.setTextSize(3);
    display.setCursor(oled_center_h(String(weight)+" g"), OLED_DATA_START+10);
    display.print(weight);
    display.print(" g");
    display.display();
}
