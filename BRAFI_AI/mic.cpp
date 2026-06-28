#include "mic.h"
#include "config.h"
#include <driver/i2s_std.h>
#include <math.h>
#include "splash.h"
#include "display.h"

// Handle untuk I2S RX channel baru (ESP-IDF v5 / Core v3)
i2s_chan_handle_t rx_chan = NULL;

bool initMic() {
    // 1. Buat channel I2S RX
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan) != ESP_OK) {
        Serial.println("Gagal membuat channel I2S RX");
        return false;
    }

    // 2. Konfigurasi mode standar I2S (INMP441)
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_SCK_PIN,
            .ws   = (gpio_num_t)MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)MIC_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    
    if (i2s_channel_init_std_mode(rx_chan, &rx_std_cfg) != ESP_OK) {
        Serial.println("Gagal inisialisasi I2S mode standar");
        return false;
    }
    
    if (i2s_channel_enable(rx_chan) != ESP_OK) {
        Serial.println("Gagal mengaktifkan channel I2S");
        return false;
    }

    Serial.printf("Mic I2S: SCK=%d WS=%d SD=%d\n", MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);
    
    // Buang sampel awal noise
    int32_t dummy;
    size_t bytes_read;
    for (int i=0; i<500; i++) i2s_channel_read(rx_chan, &dummy, sizeof(int32_t), &bytes_read, 0);

    return true;
}

void pauseMic() {
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
    }
}

void flushMicBuffer() {
    if (!rx_chan) return;

    int32_t dummy_sample;
    size_t dummy_bytes;
    for (int i = 0; i < VAD_POST_PLAYBACK_FLUSH; i++) {
        if (i2s_channel_read(rx_chan, &dummy_sample, sizeof(int32_t), &dummy_bytes, 0) != ESP_OK || dummy_bytes == 0) {
            break;
        }
    }
}

void resumeMic() {
    if (!rx_chan) return;

    if (i2s_channel_enable(rx_chan) != ESP_OK) {
        Serial.println("Mic: gagal enable ulang setelah pause.");
        return;
    }
    flushMicBuffer();
}

int getMicVolume() {
    int32_t sample32 = 0;
    size_t bytes_read = 0;

    if (i2s_channel_read(rx_chan, &sample32, sizeof(int32_t), &bytes_read, portMAX_DELAY) == ESP_OK) {
        if (bytes_read > 0) {
            int16_t sample16 = sample32 >> 16; // INMP441 24-bit MSB aligned to 32-bit, so shift 16 to get top 16 bits
            int volume = abs(sample16) / 50;
            if (volume > 100) volume = 100;
            return volume;
        }
    }
    return 0;
}

void writeWavHeader(uint8_t* buffer, size_t pcm_data_len) {
    uint32_t overall_size = pcm_data_len + 36;
    uint32_t byte_rate = MIC_SAMPLE_RATE * 2; 
    
    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        (uint8_t)(overall_size & 0xff), (uint8_t)((overall_size >> 8) & 0xff), (uint8_t)((overall_size >> 16) & 0xff), (uint8_t)((overall_size >> 24) & 0xff),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0, 
        1, 0, 
        1, 0, 
        (uint8_t)(MIC_SAMPLE_RATE & 0xff), (uint8_t)((MIC_SAMPLE_RATE >> 8) & 0xff), (uint8_t)((MIC_SAMPLE_RATE >> 16) & 0xff), (uint8_t)((MIC_SAMPLE_RATE >> 24) & 0xff),
        (uint8_t)(byte_rate & 0xff), (uint8_t)((byte_rate >> 8) & 0xff), (uint8_t)((byte_rate >> 16) & 0xff), (uint8_t)((byte_rate >> 24) & 0xff),
        2, 0, 
        16, 0, 
        'd', 'a', 't', 'a',
        (uint8_t)(pcm_data_len & 0xff), (uint8_t)((pcm_data_len >> 8) & 0xff), (uint8_t)((pcm_data_len >> 16) & 0xff), (uint8_t)((pcm_data_len >> 24) & 0xff)
    };
    
    memcpy(buffer, header, 44);
}

bool recordAudioVAD(uint8_t** out_audio_data, size_t* out_audio_len, bool manual_trigger) {
    const size_t MAX_RECORD_SIZE = 160000;
    uint8_t* record_buffer = (uint8_t*) heap_caps_malloc(MAX_RECORD_SIZE + 44, MALLOC_CAP_SPIRAM);
    
    if (record_buffer == NULL) {
        Serial.println("Gagal alokasi PSRAM untuk rekaman!");
        return false;
    }
    
    size_t byte_count = 0;
    
    // --- FLUSH DMA BUFFER ---
    flushMicBuffer();
    
    bool is_recording = manual_trigger;
    unsigned long last_sound_time = millis();
    unsigned long record_start_time = manual_trigger ? millis() : 0;
    
    if (manual_trigger) {
        clearDisplay();
        Adafruit_SSD1306* disp = getDisplay();
        disp->setTextSize(1);
        disp->setCursor((128 - (12 * 6)) / 2, 25);
        disp->print("Recording...");
        updateDisplay();
        Serial.println("VAD: Manual Trigger - Langsung merekam!");
    } else {
        Serial.println("VAD: Menunggu suara (Dynamic Mode)...");
    }
    
    // --- Anti-Clap & RMS Filter ---
    static float dc_offset = 0.0;
    const float alpha = 0.02; // Ditingkatkan dari 0.01 ke 0.02 untuk filter noise frekuensi rendah (AC/Kipas) yang lebih kuat
    int trigger_consecutive = 0;
    
    // --- Dynamic Noise Floor ---
    static float noise_baseline = 15.0; // Nilai awal asumsi noise ruangan
    const float noise_alpha = 0.05; // Kecepatan adaptasi terhadap perubahan noise ruangan
    
    extern QueueHandle_t chatRequestQueue;
    
    const int CHUNK_SAMPLES = 256; // 16ms per chunk pada 16000Hz
    int32_t sample_chunk[CHUNK_SAMPLES];
    
    while (true) {
        if (uxQueueMessagesWaiting(chatRequestQueue) > 0) {
            heap_caps_free(record_buffer);
            Serial.println("VAD: Batal mendengarkan karena ada chat masuk!");
            return false; 
        }
        
        size_t bytes_read = 0;
        if (i2s_channel_read(rx_chan, sample_chunk, sizeof(sample_chunk), &bytes_read, pdMS_TO_TICKS(100)) == ESP_OK) {
            int samples_read = bytes_read / sizeof(int32_t);
            if (samples_read > 0) {
                long long sum_squares = 0;
                int valid_samples = 0;
                
                // Ambil hanya channel kiri (L) dengan i += 2 karena I2S dalam mode STEREO
                for (int i = 0; i < samples_read; i += 2) {
                    int16_t raw_sample = sample_chunk[i] >> 16;
                    dc_offset = (1.0 - alpha) * dc_offset + alpha * raw_sample;
                    int16_t sample16 = raw_sample - (int16_t)dc_offset;
                    sum_squares += ((long long)sample16 * sample16);
                    
                    if (is_recording && byte_count < MAX_RECORD_SIZE) {
                        memcpy(record_buffer + 44 + byte_count, &sample16, sizeof(int16_t));
                        byte_count += sizeof(int16_t);
                    }
                    valid_samples++;
                }
                
                int rms = sqrt(sum_squares / valid_samples);
                int volume = rms / VAD_RMS_DIVISOR;
                
                if (!is_recording) {
                    static int vis_counter = 0;
                    if (vis_counter++ % 4 == 0) drawMicVisualizer(volume); 
                    
                    // Kalibrasi Noise Floor secara kontinu saat tidak merekam
                    if (volume < noise_baseline + 10) {
                        noise_baseline = (1.0 - noise_alpha) * noise_baseline + (noise_alpha * volume);
                    }
                    
                    // Trigger dinamis berdasarkan baseline noise ruangan
                    if (volume > (noise_baseline + VAD_TRIGGER_MARGIN)) {
                        trigger_consecutive++;
                        if (trigger_consecutive >= VAD_TRIGGER_CHUNKS) {
                            is_recording = true;
                            record_start_time = millis();
                            last_sound_time = millis();
                            
                            clearDisplay();
                            Adafruit_SSD1306* disp = getDisplay();
                            disp->setTextSize(1);
                            disp->setCursor((128 - (12 * 6)) / 2, 25);
                            disp->print("Recording...");
                            updateDisplay();
                            Serial.printf("VAD: Suara terdeteksi! (Vol: %d, Baseline: %.1f)\n", volume, noise_baseline);
                        }
                    } else {
                        if (trigger_consecutive > 0) trigger_consecutive--;
                    }
                } else {
                    if (volume > (noise_baseline + VAD_SILENCE_MARGIN)) {
                        last_sound_time = millis();
                    } else if (millis() - last_sound_time > VAD_SILENCE_TIMEOUT_MS) {
                        Serial.println("VAD: Diam terdeteksi, menghentikan rekaman.");
                        break;
                    }

                    if (millis() - record_start_time > VAD_MAX_RECORD_MS) {
                        Serial.println("VAD: Batas durasi rekaman tercapai.");
                        break;
                    }
                    
                    if (byte_count >= MAX_RECORD_SIZE) {
                        Serial.println("VAD: Memori penuh, menghentikan rekaman.");
                        break; 
                    }
                }
            }
        }
    }
    
    if (byte_count == 0) {
        heap_caps_free(record_buffer);
        return false;
    }
    
    writeWavHeader(record_buffer, byte_count);
    *out_audio_data = record_buffer;
    *out_audio_len = byte_count + 44;
    return true;
}
