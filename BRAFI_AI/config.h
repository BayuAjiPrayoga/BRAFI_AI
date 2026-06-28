#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// KONFIGURASI PIN & HARDWARE
// ==========================================

// --- OLED Display (I2C) ---
#define OLED_SDA_PIN    8
#define OLED_SCL_PIN    9
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDRESS    0x3C

// --- I2S Audio (MAX98357A - Speaker, I2S port 0) ---
// Pin asli yang sudah terbukti bunyi. Jangan ubah kecuali kabel ikut dipindah!
#define I2S_BCLK_PIN    15
#define I2S_LRC_PIN     16
#define I2S_DOUT_PIN    7

// --- I2S Audio (INMP441 - Mic, I2S port 1) ---
// WS di GPIO 4 (bukan 15) supaya tidak bentrok dengan speaker BCLK.
#define MIC_WS_PIN      4
#define MIC_SCK_PIN     14
#define MIC_SD_PIN      13
#define MIC_SAMPLE_RATE 16000

// --- VAD (Voice Activity Detection) ---
#define VAD_TRIGGER_MARGIN         20  // Selisih volume (di atas noise floor) untuk mulai rekam
#define VAD_SILENCE_MARGIN         10  // Selisih volume untuk menganggap hening
#define VAD_SILENCE_TIMEOUT_MS     1500 
#define VAD_TRIGGER_CHUNKS         15
#define VAD_MAX_RECORD_MS          12000
#define VAD_POST_PLAYBACK_FLUSH    4000
#define VAD_RMS_DIVISOR            22

// --- Audio Format ---
#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16

// --- Konfigurasi WiFi & API (Tahap 3) ---
// Fase 5: WiFi credentials dihapus dari source code — disimpan di Preferences ESP32
// Gunakan Captive Portal (BRAFI_AI_SETUP) untuk setup WiFi pertama kali
#define WIFI_SSID       ""
#define WIFI_PASSWORD   ""
#define DEFAULT_BACKEND_URL "http://192.168.1.50:8000"

// --- Fase 5: AES Encryption Constants ---
#define AES_SALT "BRAFI_AI_2025"

// --- Fase 6: FreeRTOS Queue Struct ---
struct ChatMessage {
    char request[512];
    char reply[1024];
};

#endif // CONFIG_H
