#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

/**
 * @brief Inisialisasi driver I2S speaker (log pin config)
 */
bool initAudio();

/**
 * @brief Putar buffer WAV (RIFF/WAVE PCM 16-bit) ke speaker MAX98357A
 * @param gain Volume 0.0 - 1.0
 */
bool playWavBuffer(const uint8_t* wavData, size_t wavLen, float gain = 0.5f);

/**
 * @brief Memutar nada test beep 440Hz (0.5 detik)
 */
void playTestTone();

/**
 * @brief Memutar suara welcome dari PROGMEM
 */
void playWav(const char* filename);

/**
 * @brief Memutar nada berpikir (Do Mi Sol Mi, Do Mi Sol Do')
 */
void playThinkingMelody();

/**
 * @brief Matikan channel I2S speaker TX
 */
void stopSpeakerTx();

#endif // AUDIO_H
