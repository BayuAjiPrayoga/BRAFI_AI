#ifndef MIC_H
#define MIC_H

#include <Arduino.h>

/**
 * @brief Inisialisasi I2S untuk mikrofon INMP441
 * @return true jika berhasil, false jika gagal
 */
bool initMic();

/**
 * @brief Membaca sampel audio dari mikrofon dan menghitung level volume
 * @return Nilai volume rata-rata (RMS) skala 0 - 100
 */
int getMicVolume();

/**
 * @brief Merekam suara ke dalam PSRAM secara dinamis berdasarkan VAD
 * @param out_audio_data Pointer untuk menampung alamat buffer WAV yang dibuat di PSRAM
 * @param out_audio_len Pointer untuk menampung panjang data WAV dalam byte
 * @return true jika berhasil merekam suara, false jika gagal/buffer penuh
 */
bool recordAudioVAD(uint8_t** out_audio_data, size_t* out_audio_len, bool manual_trigger = false);

/** Matikan sementara I2S mic (mis. saat speaker/TTS aktif) */
void pauseMic();

/** Nyalakan kembali I2S mic dan flush buffer DMA */
void resumeMic();

#endif // MIC_H
