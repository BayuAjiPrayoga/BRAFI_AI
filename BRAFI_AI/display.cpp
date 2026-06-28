#include "display.h"
#include "config.h"
#include <Wire.h>

// Deklarasi global instance display
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
SemaphoreHandle_t displayMutex = NULL;

bool initDisplay() {
    displayMutex = xSemaphoreCreateMutex();
    
    // Inisialisasi pin I2C khusus untuk ESP32
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    // Inisialisasi OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        return false;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
    return true;
}

Adafruit_SSD1306* getDisplay() {
    return &display;
}

void clearDisplay() {
    display.clearDisplay();
}

void updateDisplay() {
    display.display();
}
