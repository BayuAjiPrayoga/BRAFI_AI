/**
 * BRAFI AI Assistant - Tahap 1
 * 
 * Hardware:
 * - ESP32-S3 DevKit
 * - OLED SSD1306 (I2C)
 * - MAX98357A I2S Amplifier
 */

#include <Arduino.h>
#include "config.h"
#include "filesystem.h"
#include "display.h"
#include "splash.h"
#include "audio.h"
#include "mic.h"
#include "network.h"
#include "gemini.h"
#include "tts.h"
#include "web.h"
#include "mqtt.h"
#include "face_engine.h"
#include <Preferences.h>
#include <esp_system.h>

const char* current_ai_status = "Ready";
TaskHandle_t webTaskHandle;

// --- Fase 6: FreeRTOS Queue (struct ChatMessage di config.h) ---
QueueHandle_t chatRequestQueue;   // Core 0 → Core 1 (request)
QueueHandle_t chatReplyQueue;     // Core 1 → Core 0 (reply)

FaceEngine* faceEngine = nullptr;
TaskHandle_t faceTaskHandle;

volatile bool button_pressed_short = false; // Flag trigger PTT (Push to Talk)

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

// Task FreeRTOS untuk Web Server (Berjalan di Core 0)
void webTask(void * pvParameters) {
    while(true) {
        handleWiFiLoop();
        server.handleClient();
        mqttLoop();  // Fase 2: MQTT loop di Core 0
        
        // Cek tombol BOOT (GPIO 0) untuk Reset WiFi / PTT
        if (digitalRead(0) == LOW) {
            unsigned long pressTime = millis();
            while (digitalRead(0) == LOW) { vTaskDelay(10 / portTICK_PERIOD_MS); } // Tunggu dilepas
            
            unsigned long duration = millis() - pressTime;
            // Jika ditekan lebih dari 2 detik (Reset)
            if (duration > 2000) {
                Serial.println("BOOT Button Hold: Resetting WiFi...");
                Preferences prefs;
                prefs.begin("wifi_creds", false);
                prefs.clear();
                prefs.end();
                ESP.restart();
            } else if (duration > 50) { // Jika ditekan sebentar (Debounce 50ms)
                Serial.println("BOOT Button Short Press: Trigger Voice!");
                button_pressed_short = true;
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS); // Beri nafas untuk Core 0
    }
}

// Task FreeRTOS untuk Face Engine Animasi (Berjalan di Core 0 agar tidak mengganggu audio)
void faceTask(void * pvParameters) {
    while(true) {
        if (faceEngine != nullptr) {
            String status = String(current_ai_status);
            if (status == "Ready") {
                faceEngine->setState(FaceState::Idle);
                faceEngine->update();
            } else if (status == "Listening...") {
                faceEngine->setState(FaceState::Listening);
                faceEngine->update();
            } else if (status == "Thinking...") {
                faceEngine->setState(FaceState::Thinking);
                faceEngine->update();
            } else if (status == "Speaking...") {
                faceEngine->setState(FaceState::Speaking);
                faceEngine->update();
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS); // ~20 FPS
    }
}

void setup() {
    Serial.begin(115200);
    
    // Inisialisasi Tombol BOOT
    pinMode(0, INPUT_PULLUP);
    delay(1000); // Waktu untuk Serial Monitor
    Serial.println("\n--- Starting BRAFI AI ---");
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("Reset reason=%d (%s)\n", reset_reason, resetReasonName(reset_reason));

    // 1. Inisialisasi File System (SPIFFS)
    if (!initFS()) {
        Serial.println("HALT: SPIFFS Inisialisasi Gagal.");
        while (true) delay(1000);
    }

    // 2. Inisialisasi Layar OLED
    if (!initDisplay()) {
        Serial.println("HALT: OLED Inisialisasi Gagal.");
        while (true) delay(1000);
    }
    
    // Inisialisasi Face Engine
    faceEngine = new FaceEngine(getDisplay());
    // Ambil preferensi wajah jika ada
    Preferences prefs;
    prefs.begin("face", true); // read-only
    int ew = prefs.getInt("ew", 12);
    int eh = prefs.getInt("eh", 16);
    int esp = prefs.getInt("esp", 16);
    int mw = prefs.getInt("mw", 14);
    prefs.end();
    faceEngine->setFaceParameters(ew, eh, esp, mw, 3, -6, 18);

    // 3. Tampilkan Splash Screen
    showSplashScreen();

    // 4. Inisialisasi Audio I2S
    if (!initAudio()) {
        Serial.println("HALT: I2S Audio Inisialisasi Gagal.");
        while (true) delay(1000);
    }

    // TEST HARDWARE I2S: Dimatikan — sudah terbukti bekerja
    // playTestTone();
    // delay(500);

    // 5. Putar suara "Halo, Selamat Datang"
    // Pastikan file "welcome.wav" ada di SPIFFS
    playWav("/welcome.wav");

    // 6. Tampilkan Status Ready sejenak sebelum masuk ke Listening mode
    // (Digantikan oleh FaceEngine)
    // showReadyScreen();
    delay(2000);

    // 7. Inisialisasi Mikrofon INMP441 (I2S port 1, terpisah dari speaker port 0)
    if (!initMic()) {
        Serial.println("HALT: Mikrofon Inisialisasi Gagal.");
        while (true) delay(1000);
    }
    
    // 8. Inisialisasi WiFi (Tahap 3)
    initWiFi();
    
    // 9. Inisialisasi MQTT (Fase 2)
    initMQTT();
    
    // 10. Buat FreeRTOS Queue (Fase 6)
    chatRequestQueue = xQueueCreate(1, sizeof(ChatMessage));
    chatReplyQueue   = xQueueCreate(1, sizeof(ChatMessage));
    
    // 11. Jalankan Web Server di Core 0 (Multi-threading / RTOS)
    xTaskCreatePinnedToCore(
        webTask,        // Fungsi Task
        "WebTask",      // Nama Task
        8192,           // Ukuran Stack
        NULL,           // Parameter
        1,              // Prioritas
        &webTaskHandle, // Handle
        0               // Dijalankan di Core 0
    );

    // 12. Jalankan Face Engine di Core 0
    xTaskCreatePinnedToCore(
        faceTask,
        "FaceTask",
        4096,
        NULL,
        1,
        &faceTaskHandle,
        0
    );
}

void loop() {
    // Timer untuk publish heap via MQTT setiap 10 detik
    static unsigned long lastHeapPublish = 0;
    
    // Jika sedang dalam mode AP (Setup WiFi), hentikan proses AI
    if (is_ap_mode) {
        delay(10);
        return;
    }
    
    // --- Fase 2: Publish heap setiap 10 detik ---
    if (millis() - lastHeapPublish > 10000) {
        lastHeapPublish = millis();
        mqttPublishHeap();
    }
    
    // --- Fase 6: Terima request dari Core 0 via Queue ---
    ChatMessage msg;
    if (xQueueReceive(chatRequestQueue, &msg, 0) == pdTRUE) {
        String req = String(msg.request);
        
        Serial.println("Core 1: Memproses chat web: " + req);
        
        current_ai_status = "Thinking...";
        playThinkingMelody();
        
        // Tanya gemini
        String reply = askGeminiText(req);
        Serial.println("Core 1: Balasan Gemini: " + reply);
        
        // BUG FIX #1: Cek [IGNORE] agar tidak dikirim ke browser dan tidak di-TTS-kan
        if (reply.indexOf("[IGNORE]") != -1) {
            ChatMessage replyMsg;
            memset(&replyMsg, 0, sizeof(replyMsg));
            strncpy(replyMsg.reply, "(Diabaikan)", sizeof(replyMsg.reply) - 1);
            xQueueSend(chatReplyQueue, &replyMsg, 0);
            current_ai_status = "Ready";
            // showReadyScreen();
            delay(500);
            return;
        }
        
        // Kirim balasan kembali ke Core 0 via Queue
        ChatMessage replyMsg;
        memset(&replyMsg, 0, sizeof(replyMsg));
        strncpy(replyMsg.reply, reply.c_str(), sizeof(replyMsg.reply) - 1);
        xQueueSend(chatReplyQueue, &replyMsg, 0);
        // showTextScreen(reply);
        
        // current_ai_status = "Speaking..."; (Dipindah ke tts.cpp agar sinkron)
        playTTS(reply);
        
        current_ai_status = "Ready";
        delay(2000);
        return; // Ulangi loop dari awal
    }

    // Cek apakah mode Push-To-Talk ditekan
    if (button_pressed_short) {
        button_pressed_short = false;
        current_ai_status = "Listening...";
        
        uint8_t* audioData = NULL;
        size_t audioLen = 0;
        
        // Panggil VAD dengan mode manual_trigger = true (langsung merekam)
        if (recordAudioVAD(&audioData, &audioLen, true)) {
        
        current_ai_status = "Thinking...";
        playThinkingMelody();
        
        // Kirim audio ke Gemini API dan dapatkan balasannya
        String reply = askGeminiAudio(audioData, audioLen);
        
        if (reply.indexOf("[IGNORE]") != -1) {
            Serial.println("Core 1: Audio diabaikan (Tidak ada sapaan / Hanya noise).");
            current_ai_status = "Ready";
            // showReadyScreen();
        } else {
            // Tampilkan balasan AI di layar OLED
            // showTextScreen(reply);
            
            // current_ai_status = "Speaking..."; (Dipindah ke tts.cpp agar sinkron)
            // Bacakan jawaban AI ke speaker! (Tahap 4)
            playTTS(reply);
            
            // Tahan layar sejenak
            delay(2000);
        }
        
        current_ai_status = "Ready";
        
        // Mencegah Memory Leak: Hapus rekaman audio dari PSRAM setelah selesai!
        if (audioData != NULL) {
            heap_caps_free(audioData);
            audioData = NULL;
        }

        }
    } else {
        // Tampilkan "Ready" jika tidak ada aktivitas
        if (String(current_ai_status) != "Ready") {
            current_ai_status = "Ready";
            // showReadyScreen();
        }
    }
    
    // Delay kecil untuk stabilitas *watchdog timer*
    delay(10);
}
