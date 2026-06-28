#include "audio.h"
#include "config.h"
#include "welcome_wav.h"
#include <driver/i2s_std.h>
#include <math.h>

static i2s_chan_handle_t tx_chan = NULL;
static uint32_t tx_sample_rate = 0;

struct ParsedWav {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    const uint8_t* pcm;
    size_t pcm_bytes;
};

static bool parseWav(const uint8_t* data, size_t len, ParsedWav* out) {
    if (len < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t pos = 12;
    uint16_t audio_format = 0;
    bool have_fmt = false;

    while (pos + 8 <= len) {
        const char* chunkId = (const char*)(data + pos);
        uint32_t chunkSize = (uint32_t)data[pos + 4]
            | ((uint32_t)data[pos + 5] << 8)
            | ((uint32_t)data[pos + 6] << 16)
            | ((uint32_t)data[pos + 7] << 24);
        pos += 8;

        if (pos + chunkSize > len) {
            break;
        }

        if (memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            audio_format = (uint16_t)(data[pos] | (data[pos + 1] << 8));
            out->channels = (uint16_t)(data[pos + 2] | (data[pos + 3] << 8));
            out->sample_rate = (uint32_t)data[pos + 4]
                | ((uint32_t)data[pos + 5] << 8)
                | ((uint32_t)data[pos + 6] << 16)
                | ((uint32_t)data[pos + 7] << 24);
            out->bits_per_sample = (uint16_t)(data[pos + 14] | (data[pos + 15] << 8));
            have_fmt = true;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            if (!have_fmt || audio_format != 1) {
                Serial.println("Speaker: WAV bukan PCM.");
                return false;
            }
            if (out->bits_per_sample != 16) {
                Serial.println("Speaker: hanya WAV 16-bit yang didukung.");
                return false;
            }
            out->pcm = data + pos;
            out->pcm_bytes = chunkSize;
            return true;
        }

        pos += chunkSize + (chunkSize & 1);
    }

    return false;
}

static bool initSpeakerTx(uint32_t sample_rate) {
    if (tx_chan && tx_sample_rate == sample_rate) {
        return true;
    }

    stopSpeakerTx();

    i2s_chan_config_t tx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    tx_cfg.dma_desc_num = 8;
    tx_cfg.dma_frame_num = 512;

    if (i2s_new_channel(&tx_cfg, &tx_chan, NULL) != ESP_OK) {
        Serial.println("Speaker: gagal buat channel I2S TX.");
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws = (gpio_num_t)I2S_LRC_PIN,
            .dout = (gpio_num_t)I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(tx_chan, &std_cfg) != ESP_OK) {
        Serial.println("Speaker: gagal init mode I2S.");
        stopSpeakerTx();
        return false;
    }

    if (i2s_channel_enable(tx_chan) != ESP_OK) {
        Serial.println("Speaker: gagal enable channel I2S.");
        stopSpeakerTx();
        return false;
    }

    tx_sample_rate = sample_rate;
    Serial.printf(
        "Speaker I2S TX: rate=%u BCLK=%d LRC=%d DOUT=%d\n",
        sample_rate, I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN
    );
    return true;
}

void stopSpeakerTx() {
    if (tx_chan) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    tx_sample_rate = 0;
}

static int16_t applyGain(int16_t sample, float gain) {
    int32_t scaled = (int32_t)(sample * gain);
    if (scaled > 32767) return 32767;
    if (scaled < -32768) return -32768;
    return (int16_t)scaled;
}

static bool playPcmMono16(const int16_t* mono, size_t sample_count, uint32_t sample_rate, float gain) {
    if (!mono || sample_count == 0) return false;
    if (!initSpeakerTx(sample_rate)) return false;

    const size_t chunk_samples = 256;
    int16_t stereo_buf[chunk_samples * 2];

    size_t offset = 0;
    while (offset < sample_count) {
        size_t n = sample_count - offset;
        if (n > chunk_samples) n = chunk_samples;

        for (size_t i = 0; i < n; i++) {
            int16_t s = applyGain(mono[offset + i], gain);
            stereo_buf[i * 2] = s;
            stereo_buf[i * 2 + 1] = s;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            tx_chan,
            stereo_buf,
            n * 2 * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
        if (err != ESP_OK) {
            Serial.printf("Speaker: i2s_channel_write gagal (%d)\n", err);
            return false;
        }

        offset += n;
        yield();
    }

    return true;
}

static bool playPcmStereo16(const int16_t* stereo, size_t frame_count, uint32_t sample_rate, float gain) {
    if (!stereo || frame_count == 0) return false;
    if (!initSpeakerTx(sample_rate)) return false;

    const size_t chunk_frames = 256;
    int16_t out_buf[chunk_frames * 2];

    size_t offset = 0;
    while (offset < frame_count) {
        size_t n = frame_count - offset;
        if (n > chunk_frames) n = chunk_frames;

        for (size_t i = 0; i < n; i++) {
            out_buf[i * 2] = applyGain(stereo[(offset + i) * 2], gain);
            out_buf[i * 2 + 1] = applyGain(stereo[(offset + i) * 2 + 1], gain);
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            tx_chan,
            out_buf,
            n * 2 * sizeof(int16_t),
            &bytes_written,
            portMAX_DELAY
        );
        if (err != ESP_OK) {
            Serial.printf("Speaker: i2s_channel_write gagal (%d)\n", err);
            return false;
        }

        offset += n;
        yield();
    }

    return true;
}

bool initAudio() {
    Serial.printf(
        "Speaker config: BCLK=%d LRC=%d DOUT=%d\n",
        I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN
    );
    return true;
}

bool playWavBuffer(const uint8_t* wavData, size_t wavLen, float gain) {
    ParsedWav wav = {};
    if (!parseWav(wavData, wavLen, &wav)) {
        Serial.println("Speaker: parse WAV gagal.");
        return false;
    }

    Serial.printf(
        "Speaker: WAV rate=%u ch=%u bits=%u bytes=%u\n",
        wav.sample_rate, wav.channels, wav.bits_per_sample, wav.pcm_bytes
    );

    if (wav.channels == 1) {
        size_t samples = wav.pcm_bytes / sizeof(int16_t);
        return playPcmMono16((const int16_t*)wav.pcm, samples, wav.sample_rate, gain);
    }

    if (wav.channels == 2) {
        size_t frames = wav.pcm_bytes / (2 * sizeof(int16_t));
        return playPcmStereo16((const int16_t*)wav.pcm, frames, wav.sample_rate, gain);
    }

    Serial.println("Speaker: jumlah channel WAV tidak didukung.");
    return false;
}

void playTestTone() {
    const int sample_rate = 16000;
    const int duration_samples = sample_rate / 2;
    int16_t* tone = (int16_t*)malloc(duration_samples * sizeof(int16_t));
    if (!tone) {
        Serial.println("Speaker: gagal alokasi buffer test tone.");
        return;
    }

    for (int i = 0; i < duration_samples; i++) {
        tone[i] = (int16_t)(7000.0 * sin(2.0 * M_PI * 440.0 * i / sample_rate));
    }

    Serial.println("Speaker: test beep 440Hz...");
    if (playPcmMono16(tone, duration_samples, sample_rate, 0.35f)) {
        Serial.println("Speaker: test beep selesai.");
    } else {
        Serial.println("Speaker: test beep gagal.");
    }

    free(tone);
}

void playWav(const char* filename) {
    (void)filename;
    Serial.println("Speaker: memutar welcome...");
    if (playWavBuffer(welcome_wav, welcome_wav_len, 0.5f)) {
        Serial.println("Speaker: welcome selesai.");
    } else {
        Serial.println("Speaker: welcome gagal.");
    }
}

void playThinkingMelody() {
    const int sample_rate = 16000;
    const int duration_ms = 120; // durasi per nada
    const int samples = (sample_rate * duration_ms) / 1000;
    
    int16_t* tone = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!tone) return;
    
    // Frekuensi nada
    float Do = 523.25;
    float Mi = 659.25;
    float Sol = 783.99;
    float DoTinggi = 1046.50;
    
    // Urutan: Do, Mi, Sol, Mi,   Do, Mi, Sol, Do'
    float melody[] = {Do, Mi, Sol, Mi, Do, Mi, Sol, DoTinggi};
    
    for (int n = 0; n < 8; n++) {
        float freq = melody[n];
        for (int i = 0; i < samples; i++) {
            // Envelope sederhana untuk mengurangi 'klik'
            float env = 1.0;
            if (i < 200) env = i / 200.0;
            if (i > samples - 200) env = (samples - i) / 200.0;
            
            tone[i] = (int16_t)(7000.0 * env * sin(2.0 * M_PI * freq * i / sample_rate));
        }
        playPcmMono16(tone, samples, sample_rate, 0.25f);
    }
    
    free(tone);
}
