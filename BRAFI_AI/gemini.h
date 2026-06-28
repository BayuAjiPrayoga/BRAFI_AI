#ifndef GEMINI_H
#define GEMINI_H

#include <Arduino.h>

/**
 * @brief Mengirimkan audio ke Gemini API dan mengembalikan balasannya.
 */
String askGeminiAudio(const uint8_t* audioData, size_t audioLen);

/**
 * @brief Mengirimkan teks ke Gemini API dan mengembalikan balasannya (Untuk Web Chat).
 */
String askGeminiText(const String& textPrompt);

extern String latest_audio_url;

#endif // GEMINI_H
