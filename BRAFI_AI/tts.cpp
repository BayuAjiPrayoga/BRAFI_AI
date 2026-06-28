#include "tts.h"
#include "config.h"
#include "gemini.h"
#include "audio.h"
#include "mic.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

extern const char* current_ai_status;

static const size_t TTS_INITIAL_DOWNLOAD_BUFFER_SIZE = 64 * 1024;
static const size_t TTS_DOWNLOAD_CHUNK_SIZE = 2048;
static const size_t TTS_MAX_AUDIO_SIZE = 2 * 1024 * 1024;
static const uint32_t TTS_DOWNLOAD_IDLE_TIMEOUT_MS = 15000;
static const float TTS_OUTPUT_GAIN = 0.5f;

static void logTTSMemory(const char* label) {
    Serial.printf(
        "TTS MEM [%s] heap_free=%u heap_min=%u heap_largest=%u psram_free=%u psram_largest=%u\n",
        label,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)
    );
}

static void* allocAudioBuffer(size_t size) {
    void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return buffer;
}

static bool growAudioBuffer(uint8_t** buffer, size_t* capacity, size_t required) {
    if (required <= *capacity) return true;
    if (required > TTS_MAX_AUDIO_SIZE) return false;

    size_t newCapacity = *capacity;
    while (newCapacity < required) {
        newCapacity *= 2;
        if (newCapacity > TTS_MAX_AUDIO_SIZE) {
            newCapacity = TTS_MAX_AUDIO_SIZE;
            break;
        }
    }

    void* grown = heap_caps_realloc(*buffer, newCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!grown) {
        grown = heap_caps_realloc(*buffer, newCapacity, MALLOC_CAP_8BIT);
    }
    if (!grown) return false;

    *buffer = (uint8_t*)grown;
    *capacity = newCapacity;
    return true;
}

static bool downloadTTSAudio(const String& url, uint8_t** audioData, size_t* audioLen) {
    *audioData = nullptr;
    *audioLen = 0;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(20000);

    if (!http.begin(client, url)) {
        Serial.println("TTS Download Failed: URL tidak valid.");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.println("TTS Download Failed, HTTP code: " + String(httpCode));
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength > 0 && (size_t)contentLength > TTS_MAX_AUDIO_SIZE) {
        Serial.println("TTS Download Failed: file audio terlalu besar.");
        http.end();
        return false;
    }

    size_t capacity = contentLength > 0 ? (size_t)contentLength : TTS_INITIAL_DOWNLOAD_BUFFER_SIZE;
    if (capacity == 0) capacity = TTS_INITIAL_DOWNLOAD_BUFFER_SIZE;

    uint8_t* buffer = (uint8_t*)allocAudioBuffer(capacity);
    if (!buffer) {
        Serial.println("TTS Download Failed: memori tidak cukup untuk audio.");
        http.end();
        return false;
    }

    uint8_t chunk[TTS_DOWNLOAD_CHUNK_SIZE];
    size_t total = 0;
    unsigned long lastDataTime = millis();
    auto* stream = http.getStreamPtr();

    while (http.connected() && (contentLength < 0 || total < (size_t)contentLength)) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = available;
            if (toRead > sizeof(chunk)) toRead = sizeof(chunk);

            int bytesRead = stream->readBytes(chunk, toRead);
            if (bytesRead > 0) {
                if (!growAudioBuffer(&buffer, &capacity, total + bytesRead)) {
                    Serial.println("TTS Download Failed: buffer audio penuh.");
                    heap_caps_free(buffer);
                    http.end();
                    return false;
                }
                memcpy(buffer + total, chunk, bytesRead);
                total += bytesRead;
                lastDataTime = millis();
            }
        } else {
            if (millis() - lastDataTime > TTS_DOWNLOAD_IDLE_TIMEOUT_MS) {
                Serial.println("TTS Download Failed: timeout menunggu data audio.");
                heap_caps_free(buffer);
                http.end();
                return false;
            }
            delay(2);
            yield();
        }
    }

    http.end();

    if (contentLength > 0 && total != (size_t)contentLength) {
        Serial.println("TTS Download Failed: audio tidak lengkap.");
        heap_caps_free(buffer);
        return false;
    }
    if (total < 16) {
        Serial.println("TTS Download Failed: data audio terlalu kecil.");
        heap_caps_free(buffer);
        return false;
    }

    *audioData = buffer;
    *audioLen = total;
    Serial.println("TTS Download OK, bytes: " + String(total));
    return true;
}

static bool isWavData(const uint8_t* data, size_t len) {
    return len >= 12
        && memcmp(data, "RIFF", 4) == 0
        && memcmp(data + 8, "WAVE", 4) == 0;
}

void playTTS(const String& text) {
    if (text.length() == 0 || text.startsWith("Err:")) return;
    if (latest_audio_url == "") {
        Serial.println("TTS Batal: URL Audio kosong dari backend.");
        return;
    }

    Serial.println("TTS Speaking from: " + latest_audio_url);
    logTTSMemory("before");

    pauseMic();

    uint8_t* audioData = nullptr;
    size_t audioLen = 0;
    if (!downloadTTSAudio(latest_audio_url, &audioData, &audioLen)) {
        logTTSMemory("download-failed");
        resumeMic();
        return;
    }

    if (!isWavData(audioData, audioLen)) {
        Serial.println("TTS Gagal: backend harus mengirim file WAV.");
        heap_caps_free(audioData);
        resumeMic();
        return;
    }

    // Ubah status jadi Speaking TEPAT SEBELUM suara keluar (agar animasi mulut sinkron)
    current_ai_status = "Speaking...";

    if (!playWavBuffer(audioData, audioLen, TTS_OUTPUT_GAIN)) {
        Serial.println("TTS playback gagal.");
    }

    heap_caps_free(audioData);
    resumeMic();

    Serial.println("TTS Selesai.");
    logTTSMemory("after");
}
