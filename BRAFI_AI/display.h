#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t displayMutex;

/**
 * @brief Inisialisasi layar OLED
 * @return true jika berhasil, false jika gagal
 */
bool initDisplay();

/**
 * @brief Mendapatkan instance dari objek display
 * @return Pointer ke objek Adafruit_SSD1306
 */
Adafruit_SSD1306* getDisplay();

/**
 * @brief Membersihkan buffer layar OLED
 */
void clearDisplay();

/**
 * @brief Mendorong buffer layar ke OLED agar tampil
 */
void updateDisplay();

#endif // DISPLAY_H
