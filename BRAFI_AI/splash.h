#ifndef SPLASH_H
#define SPLASH_H

#include <Arduino.h>

/**
 * @brief Menampilkan logo dan animasi loading saat boot
 */
void showSplashScreen();

/**
 * @brief Menampilkan status sistem Ready
 */
void showReadyScreen();

/**
 * @brief Menampilkan animasi visualizer suara mikrofon
 * @param volume Nilai volume (0 - 100)
 */
void drawMicVisualizer(int volume);

/**
 * @brief Menampilkan tulisan "Thinking..." saat menunggu AI
 */
void showThinkingScreen();

/**
 * @brief Menampilkan teks jawaban dari AI
 * @param text Jawaban AI
 */
void showTextScreen(const String& text);

#endif // SPLASH_H
