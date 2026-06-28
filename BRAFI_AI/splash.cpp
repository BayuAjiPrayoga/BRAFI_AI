#include "splash.h"
#include "display.h"

void showSplashScreen() {
    Adafruit_SSD1306* disp = getDisplay();
    if (!disp) return;

    // Menampilkan tulisan awal dengan animasi loading sederhana
    for (int i = 0; i < 3; i++) {
        clearDisplay();
        
        // Judul Utama
        disp->setTextSize(2);
        disp->setCursor((128 - (8 * 12)) / 2, 10); // Center "BRAFI AI" (8 huruf x ~12 pixel)
        disp->print("BRAFI AI");

        // Subtitle
        disp->setTextSize(1);
        disp->setCursor((128 - (15 * 6)) / 2, 40); // Center "Initializing..."
        disp->print("Initializing...");

        // Loading bar sederhana
        int barWidth = map(i, 0, 2, 10, 100);
        disp->drawRect(14, 55, 100, 5, SSD1306_WHITE);
        disp->fillRect(14, 55, barWidth, 5, SSD1306_WHITE);

        updateDisplay();
        delay(500);
    }
}

void showReadyScreen() {
    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Adafruit_SSD1306* disp = getDisplay();
            if (disp) {
                clearDisplay();
                
                // Judul
                disp->setTextSize(2);
                disp->setCursor((128 - (8 * 12)) / 2, 0); 
                disp->print("BRAFI AI");

                // Status Ready
                disp->setTextSize(1);
                disp->setCursor((128 - (5 * 6)) / 2, 25);
                disp->print("READY");

                // Pesan Bawah
                disp->setTextSize(1);
                disp->setCursor((128 - (19 * 6)) / 2, 45); 
                disp->print("Halo Selamat Datang");

                updateDisplay();
            }
            xSemaphoreGive(displayMutex);
        }
    }
}

void drawMicVisualizer(int volume) {
    Adafruit_SSD1306* disp = getDisplay();
    if (!disp) return;

    clearDisplay();
    
    // Header
    disp->setTextSize(1);
    disp->setCursor((128 - (12 * 6)) / 2, 10);
    disp->print("Listening...");

    // Menggambar visualizer di tengah layar
    int barWidth = volume;
    if (barWidth > 100) barWidth = 100;
    
    // Agar bar mengembang dari tengah
    int xPos = (128 - barWidth) / 2;
    int yPos = 35;
    int barHeight = 10;
    
    // Gambar bingkai statis panjang maksimum 100
    disp->drawRect(13, yPos - 2, 102, barHeight + 4, SSD1306_WHITE);
    
    // Gambar isian volume suara
    if (barWidth > 0) {
        disp->fillRect(xPos, yPos, barWidth, barHeight, SSD1306_WHITE);
    }

    updateDisplay();
}

void showThinkingScreen() {
    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Adafruit_SSD1306* disp = getDisplay();
            if (disp) {
                clearDisplay();
                disp->setTextSize(1);
                disp->setCursor((128 - (11 * 6)) / 2, 30); // Center "Thinking..."
                disp->print("Thinking...");
                updateDisplay();
            }
            xSemaphoreGive(displayMutex);
        }
    }
}

void showTextScreen(const String& text) {
    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Adafruit_SSD1306* disp = getDisplay();
            if (disp) {
                clearDisplay();
                disp->setTextSize(1);
                disp->setCursor(0, 0);
                disp->print(text);
                updateDisplay();
            }
            xSemaphoreGive(displayMutex);
        }
    }
}
