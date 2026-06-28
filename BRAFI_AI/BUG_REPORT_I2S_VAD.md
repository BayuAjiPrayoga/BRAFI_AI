# Dokumentasi Penyelesaian Bug Audio & VAD (BRAFI AI)

Dokumen ini mencatat penyelesaian masalah kritis terkait pemrosesan suara mikrofon INMP441 pada ESP32-S3 dan *backend* Python (Whisper AI) yang diselesaikan pada pembaruan ini.

## 1. Bug "Slow Motion" (Distorsi Kecepatan Audio)
**Gejala:** 
Suara pengguna yang dikirim ke *backend* terdengar lambat (50% *slow motion*), putus-putus, dan gagal diterjemahkan oleh Whisper AI. *Backend* secara konstan melempar *error* 500 atau pesan "API gagal memproses suara".

**Akar Masalah (Root Cause):**
- Pada ESP-IDF v5, penggunaan `I2S_SLOT_MODE_MONO` dalam format Philips secara diam-diam membuat *driver* I2S mencuplik (mengambil sampel) saluran Kiri (L) dan Kanan (R) secara bersamaan.
- Karena frekuensi *sampling* diatur ke 16.000 Hz, pembacaan stereo tanpa sengaja menghasilkan **32.000 sampel per detik** di dalam *buffer* C++.
- Ketika file WAV berdurasi 32.000 sampel per detik ini diberikan *header* 16.000 Hz (di `mic.cpp`), perangkat pemutar (*player*) maupun Whisper tertipu dan memutarnya dengan kecepatan setengahnya, sehingga Whisper AI gagal memahami kata-kata karena struktur fonetiknya hancur.

**Solusi yang Diterapkan (`mic.cpp`):**
- **Mengubah Mode ke STEREO:** `slot_cfg` diubah ke `I2S_SLOT_MODE_STEREO`.
- **Pembuangan Channel Kosong (Downmixing):** Di dalam perulangan pembacaan I2S (`i2s_channel_read`), iterasi diubah menjadi `i += 2`. Ini berarti ESP32 secara eksplisit membuang sampel Kanan dan hanya menyimpan sampel Kiri (L) ke dalam rekaman. 

## 2. Bug VAD Kurang Sensitif / Sering Terpotong
**Gejala:** 
Mikrofon harus diteriaki secara konstan tanpa jeda agar mulai merekam. Jika ada jeda antar kata, rekaman terputus. Namun jika mikrofon disentuh dengan jari, ia malah merekam (*looping* statis).

**Akar Masalah (Root Cause):**
- Logika VAD (*Voice Activity Detection*) C++ sebelumnya akan me-reset *trigger counter* kembali ke `0` secara instan setiap kali volume suara turun di bawah *threshold* walau hanya 16 milidetik (misalnya saat pengguna menghela napas antar suku kata).
- *Threshold* statis (angka `50`) tidak fleksibel terhadap *noise floor* (tingkat kebisingan dasar) ruangan.

**Solusi yang Diterapkan (`mic.cpp` & `config.h`):**
- **Sistem Decay (Toleransi Jeda):** Menghapus reset instan `trigger_consecutive = 0` dan menggantinya dengan `trigger_consecutive--` (pengurangan bertahap). Ini memberi toleransi bagi pengucapan bahasa manusia yang natural.
- **Dynamic Noise Floor:** Mengubah VAD statis menjadi adaptif. ESP32 sekarang melacak tingkat kebisingan ruangan secara diam-diam (menggunakan variabel `noise_baseline`). Mikrofon hanya akan merekam jika suara melebihi `noise_baseline + VAD_TRIGGER_MARGIN` (di set ke 20). Ini membuat mikrofon sama pekanya baik saat di kamar sepi maupun di ruang ber-AC/kipas angin.
- **Penajaman HPF:** Nilai *High-Pass Filter* (`alpha`) dinaikkan dari 0.01 ke 0.02 untuk lebih agresif memblokir suara gemuruh frekuensi sangat rendah.

## 3. Optimasi Backend & Mode Fallback
- Whisper di Python kini dijalankan tanpa `vad_filter`, karena ESP32 telah memastikan bahwa *file* WAV yang dikirim hanya berisi suara.
- Jika Gemini menganggap audio sebagai *noise* (respons kosong), sistem tidak akan lagi mengucapkan pesan *error* "API gagal memproses suara", melainkan secara senyap merespons dengan kode `[IGNORE]` dan kembali mendengarkan. 
- *Echo Loopback Test* terbukti ampuh sebagai instrumen *debugging* tingkat perangkat keras.

---
*Pembaruan ini secara resmi membawa kualitas tangkapan audio BRAFI AI setara dengan standar asisten suara komersial modern (Smart Speaker).*
