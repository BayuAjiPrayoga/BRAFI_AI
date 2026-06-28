#ifndef TTS_H
#define TTS_H

#include <Arduino.h>

/**
 * @brief Membaca teks menggunakan Google Translate TTS dan memutarnya ke Speaker
 * @param text Teks yang akan diucapkan
 */
void playTTS(const String& text);

#endif // TTS_H
