# Dokumentasi BRAFI AI

Dokumentasi ini berisi penjelasan lengkap mengenai konfigurasi, pinout hardware, struktur sistem, serta alur kerja antara perangkat keras (ESP32-S3) dan Backend (Python FastAPI) dari **BRAFI AI**.

## 1. Konfigurasi Hardware (ESP32-S3)

Berikut adalah konfigurasi pin (berdasarkan file `config.h`) yang terhubung pada ESP32-S3:

### OLED Display (I2C)
Modul layar untuk antarmuka visual (seperti animasi wajah/text).
- **SDA Pin**: GPIO 8
- **SCL Pin**: GPIO 9
- **Resolusi**: 128x64 pixel
- **Alamat I2C**: `0x3C`

### I2S Speaker (MAX98357A)
Modul amplifier I2S untuk mengeluarkan suara respon AI (Text-to-Speech). Menggunakan I2S Port 0.
- **BCLK (Bit Clock)**: GPIO 15
- **LRC (Word Select/LR Clock)**: GPIO 16
- **DIN (Data In)**: GPIO 7

### I2S Mikrofon (INMP441)
Modul mikrofon I2S untuk menangkap suara input dari pengguna. Menggunakan I2S Port 1 untuk menghindari konflik dengan BCLK pada speaker.
- **WS (Word Select)**: GPIO 4
- **SCK (Serial Clock)**: GPIO 14
- **SD (Serial Data)**: GPIO 13
- **Sample Rate**: 16,000 Hz

## 2. Voice Activity Detection (VAD) & Audio

ESP32 menggunakan Voice Activity Detection (VAD) untuk mendeteksi kapan pengguna sedang berbicara, menghindari trigger sembarang oleh suara tepukan atau noise, lalu secara otomatis merekam hingga suara berhenti:

- **Trigger Threshold**: `50` (Lebih tinggi = butuh suara lebih keras untuk mulai)
- **Silence Threshold**: `22` (Lebih rendah = butuh keheningan lebih baik untuk berhenti)
- **Silence Timeout**: `900 ms`
- **Max Record Time**: `12000 ms` (12 detik)
- **Format Audio**: 16kHz, 16-bit Mono.

## 3. Konfigurasi Jaringan & Backend Server

- WiFi diset di dalam kode ESP32-S3 (bisa juga diubah via web dashboard).
- **Default Backend URL**: `http://192.168.1.50:8000` (atau IP lain sesuai server lokal Anda berjalan).

## 4. Backend (Python/FastAPI)

Backend berfungsi sebagai otak di belakang proses NLP dan Text-to-Speech (TTS). Backend ini menggunakan *FastAPI* dan terhubung ke API Google *Gemini 2.5 Flash*.
Untuk menjalankannya:
```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

### Endpoints
1. `POST /api/chat`
   - Menerima `audio` (file `.wav` dari ESP32) atau `text` (dari Web Dashboard).
   - Memproses transkripsi & prompt untuk direspons oleh Gemini.
   - Jika pengguna memanggil/menyapa (contoh: "Halo", "BRAFI"), Gemini merespons. Jika hanya bising, akan menjawab `[IGNORE]`.
   - Respons dikonversi ke MP3 menggunakan `gTTS`, lalu secara internal dikonversi ke WAV (16kHz PCM_s16le, mono) via **FFmpeg** agar ringan diproses oleh ESP32.
   - Mengembalikan link audio URL (`.wav`) dan teks balasan.

2. `POST /api/transcribe`
   - Berfungsi untuk mentranskripsi file rekaman menjadi teks menggunakan kemampuan multimoda Gemini.

3. `GET /`
   - Health check untuk mengecek apakah server telah menyala.

## 5. Flowchart Singkat

1. **Standby**: ESP32 menginisialisasi WiFi, I2S Mic, dan I2S Speaker.
2. **Deteksi Suara (VAD)**: Mic merekam suara ketika VAD mendeteksi energi RMS lebih tinggi dari threshold.
3. **Pengiriman**: Audio `.wav` dari Mic di-upload ke endpoint `/api/chat` di Backend.
4. **Proses AI**: Backend mengirim rekaman suara ke API Gemini, mendapat balasan teks.
5. **TTS (Text-to-Speech)**: Teks balasan diubah ke WAV 16kHz (menggunakan kombinasi gTTS & FFmpeg).
6. **Playback**: ESP32 mendownload/stream file `.wav` tersebut dan memutarnya pada MAX98357A speaker.
7. **Selesai**: ESP32 kembali ke state Standby.

---
**Catatan Penting (Troubleshooting Hardware):**
- Jika terjadi noise parah pada speaker atau restart (watchdog), pastikan **kabel power I2S Speaker & Mikrofon memadai** dan hindari mencampur port I2S/pin BCLK secara sembarangan.
- Pin-pin di atas telah terbukti stabil. Jangan diubah kecuali saat kabel fisiknya ikut dipindahkan!
