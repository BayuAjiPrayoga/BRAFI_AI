# Saran Evaluasi Project BRAFI AI

## Kesimpulan Singkat

Project ini sudah cukup baik untuk level UAS/prototipe ESP32-S3 karena sudah menggabungkan hardware, web dashboard, koneksi internet, Gemini AI, audio input/output, dan FreeRTOS task. Namun project ini belum bisa disebut optimal untuk penggunaan nyata karena masih ada beberapa masalah penting pada security, stabilitas multitasking, dokumentasi, dan pemisahan konfigurasi rahasia.

Jika dinilai berdasarkan poin pada gambar, project ini sudah kuat pada:
- ESP32-S3
- HTTP/Web dashboard
- Tampilan web/mobile
- RTOS/FreeRTOS
- Large Language Model dengan Gemini
- Input/output audio

Yang masih perlu ditambah atau diperbaiki:
- MQTT belum ada.
- Machine learning lokal di device belum ada.
- Robotik belum ada.
- Security masih lemah karena API key dan password WiFi hardcoded, serta HTTPS memakai `setInsecure()`.

## Penilaian Terhadap Poin UAS

| Poin | Status | Catatan |
| --- | --- | --- |
| ESP32 | Sudah | Project memakai ESP32-S3 sebagai pusat sistem. |
| MQTT/HTTP | Sebagian | HTTP sudah ada melalui `WebServer`. MQTT belum terlihat di kode. |
| Tampilan Web/Mobile | Sudah | Dashboard web responsif sudah tersedia, termasuk chat dan settings. |
| RTOS | Sudah | Web server berjalan di task FreeRTOS pada Core 0. |
| Machine Learning | Sebagian | AI memakai Gemini cloud. Belum ada model ML lokal di ESP32. |
| LLM Gemini/ChatGPT | Sudah | Gemini dipakai untuk input teks dan audio. |
| Robotik | Belum | Belum ada kontrol motor, servo, sensor gerak, atau aktuator. |
| Security encrypt/decrypt | Belum optimal | Ada HTTPS, tetapi TLS certificate dimatikan dengan `setInsecure()`. Belum ada enkripsi data lokal/API. |

## Bagian Yang Sudah Baik

1. Arsitektur modular sudah cukup rapi.
   File dipisah berdasarkan fungsi seperti `network`, `web`, `gemini`, `mic`, `tts`, `display`, dan `audio`.

2. Sudah memakai FreeRTOS.
   Web server dipisah ke task `webTask()` sehingga dashboard tetap bisa berjalan sambil loop utama memproses AI.

3. Dashboard web sudah berguna.
   Web sudah memiliki chat, status hardware, pengaturan nama AI, dan system prompt.

4. Alur AI sudah lengkap.
   Sistem bisa merekam suara, mengirim audio ke Gemini, menampilkan jawaban ke OLED, dan membacakan jawaban lewat speaker.

5. Ada captive portal WiFi.
   Jika gagal konek WiFi, device masuk mode AP `BRAFI_AI_SETUP` agar konfigurasi lebih mudah.

6. Penggunaan PSRAM sudah diperhatikan.
   Rekaman audio dan base64 dialokasikan ke PSRAM, ini penting untuk ESP32-S3.

## Masalah Utama Yang Perlu Diperbaiki

### 1. Security masih paling lemah

Di `config.h`, SSID, password WiFi, dan Gemini API key masih ditulis langsung di source code. Ini berbahaya jika project dikumpulkan, dipush ke GitHub, atau dibagikan ke orang lain.

Saran:
- Hapus API key dan password dari source code.
- Simpan API key melalui captive portal/settings.
- Buat `config.example.h` tanpa rahasia.
- Jika API key yang sekarang pernah dibagikan, segera rotate/regenerate di Google AI Studio.

### 2. HTTPS belum aman

Di `gemini.cpp`, koneksi memakai:

```cpp
client.setInsecure();
```

Artinya ESP32 tidak memverifikasi sertifikat server. Untuk demo UAS ini masih sering dipakai agar cepat berhasil, tetapi dari sisi security belum baik.

Saran:
- Pakai root CA certificate Google.
- Buat catatan di dokumentasi jika `setInsecure()` hanya untuk demo.
- Untuk nilai security, tambahkan contoh encrypt/decrypt sederhana seperti AES untuk data lokal/settings.

### 3. MQTT belum ada

Pada gambar tertulis `MQTT/HTTP`. Project sudah punya HTTP, tetapi belum MQTT.

Saran:
- Tambahkan MQTT client, misalnya `PubSubClient`.
- Publish status device ke topic:
  - `brafi/status`
  - `brafi/audio/state`
  - `brafi/ai/reply`
- Subscribe command:
  - `brafi/command/chat`
  - `brafi/command/speak`
  - `brafi/command/reset_wifi`

Dengan ini project akan lebih kuat karena punya dua protokol komunikasi: HTTP untuk dashboard dan MQTT untuk IoT.

### 4. Sinkronisasi antar core belum benar-benar thread-safe

Variabel seperti `web_chat_pending`, `web_reply_ready`, `web_chat_request`, dan `web_chat_reply` dipakai lintas task/core. Flag sudah `volatile`, tetapi object `String` tidak otomatis aman untuk akses paralel.

Saran:
- Gunakan FreeRTOS queue untuk kirim request dari web task ke loop AI.
- Gunakan mutex/semaphore saat membaca/menulis `String`.
- Hindari operasi `String` besar lintas core tanpa proteksi.

### 5. Dokumentasi README belum mengikuti kondisi terbaru

README masih menulis "Tahap 1" dan fitur boot sound/OLED, padahal project sudah memiliki web dashboard, Gemini, mic, TTS, WiFi, dan FreeRTOS.

Saran:
- Update README agar sesuai fitur saat ini.
- Tambahkan diagram arsitektur.
- Tambahkan dependency library lengkap.
- Tambahkan alur setup WiFi dan API key.
- Tambahkan screenshot dashboard.

### 6. Machine learning masih cloud-based

Gemini termasuk LLM/AI cloud, tetapi bukan machine learning lokal pada ESP32.

Saran:
- Jika ingin memenuhi poin "Machine Learning", tambahkan ML kecil di device.
- Contoh yang realistis:
  - keyword spotting sederhana
  - wake word sederhana
  - klasifikasi suara keras/pelan
  - klasifikasi gesture sensor IMU jika ada sensor tambahan
- Bisa memakai TensorFlow Lite Micro atau model sederhana berbasis threshold/fitur audio.

### 7. Robotik belum ada

Belum terlihat kontrol robotik seperti motor DC, servo, driver motor, sensor jarak, atau aktuator.

Saran:
- Tambahkan kontrol servo sederhana via web/AI.
- Contoh command:
  - "lihat kiri"
  - "lihat kanan"
  - "maju"
  - "berhenti"
- Hardware minimal:
  - Servo SG90 untuk kepala/arah sensor
  - L298N/TB6612FNG untuk motor DC
  - HC-SR04/VL53L0X untuk jarak

## Prioritas Perbaikan

### Prioritas 1: Wajib untuk UAS

1. Amankan `config.h`.
   Jangan kumpulkan API key/password asli.

2. Update README.
   Jelaskan fitur yang sebenarnya sudah ada.

3. Tambahkan MQTT.
   Karena poin gambar menyebut MQTT/HTTP, sementara project baru HTTP.

4. Tambahkan bagian security.
   Minimal jelaskan enkripsi/dekripsi sederhana atau implementasi AES untuk menyimpan API key/settings.

### Prioritas 2: Membuat Project Lebih Kuat

1. Ganti komunikasi lintas core dengan FreeRTOS queue.
2. Tambahkan timeout dan error handling lebih jelas di semua request API.
3. Tambahkan halaman dashboard untuk status WiFi, heap/PSRAM, uptime, dan IP address.
4. Simpan log error ringkas untuk debugging.

### Prioritas 3: Nilai Tambahan

1. Tambahkan robotik sederhana dengan servo.
2. Tambahkan ML lokal sederhana.
3. Tambahkan MQTT dashboard dari Node-RED atau broker lokal.
4. Tambahkan mode offline sederhana jika internet/Gemini gagal.

## Saran Fitur Tambahan Sesuai Gambar

### MQTT

Tambahkan publish status:

```text
brafi/status = Ready / Listening / Thinking / Speaking
brafi/ip = alamat IP ESP32
brafi/heap = sisa heap
```

Tambahkan subscribe command:

```text
brafi/chat = pertanyaan dari MQTT
brafi/speak = teks untuk dibacakan
brafi/robot/servo = sudut servo
```

### Security Encrypt/Decrypt

Implementasi sederhana yang bisa ditunjukkan:
- Enkripsi API key sebelum disimpan ke Preferences.
- Dekripsi hanya saat akan dipakai.
- Tambahkan halaman web untuk mengubah API key.
- Jangan tampilkan API key penuh di Serial Monitor atau web.

Catatan: untuk demo akademik, AES lebih kuat daripada sekadar XOR/base64.

### Robotik

Tambahkan servo sebagai aktuator paling sederhana:
- Web dashboard punya slider sudut servo.
- Gemini bisa memproses perintah "gerakkan servo ke 90 derajat".
- MQTT bisa mengirim command `brafi/robot/servo`.

### Machine Learning

Tambahkan fitur ML yang ringan:
- Deteksi kata bangun sederhana.
- Klasifikasi suara tepuk tangan.
- Deteksi kondisi "sunyi", "normal", "bising".

Untuk ESP32-S3, fitur kecil seperti ini lebih realistis daripada menjalankan LLM lokal.

## Rekomendasi Struktur Versi Berikutnya

```text
BRAFI_AI/
├── BRAFI_AI.ino
├── config.example.h
├── secrets.h              # tidak dikumpulkan / tidak dipush
├── network.cpp/h
├── mqtt.cpp/h             # tambahan
├── security.cpp/h         # tambahan encrypt/decrypt
├── robot.cpp/h            # tambahan servo/motor
├── ml.cpp/h               # tambahan ML ringan
├── web.cpp/h
├── gemini.cpp/h
├── tts.cpp/h
├── mic.cpp/h
├── display.cpp/h
├── README.md
└── saran.md
```

## Apakah Project Ini Sudah Optimal?

Belum optimal, tetapi sudah bagus sebagai prototipe UAS. Nilai teknisnya sudah cukup kuat karena menggabungkan ESP32-S3, web dashboard, audio, WiFi, FreeRTOS, dan Gemini. Agar menjadi project yang lebih lengkap dan sesuai semua poin pada gambar, fokus perbaikan berikutnya adalah security, MQTT, dokumentasi, dan satu fitur tambahan nyata seperti robotik servo atau ML lokal sederhana.

Urutan perbaikan terbaik:

1. Amankan API key/password.
2. Update README.
3. Tambahkan MQTT.
4. Tambahkan security encrypt/decrypt.
5. Rapikan komunikasi antar task dengan queue/mutex.
6. Tambahkan robotik atau ML lokal sebagai nilai tambah.

