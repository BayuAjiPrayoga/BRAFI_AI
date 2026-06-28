# Panduan Menjalankan Sistem BRAFI AI

Sistem BRAFI AI saat ini menggunakan arsitektur terpisah untuk mengoptimalkan kinerja:
1. **ESP32-S3** bertindak sebagai antarmuka perangkat keras (Mikrofon, Speaker, dan Layar OLED).
2. **Python Backend (Lokal)** bertindak sebagai "Otak" AI, memproses audio, menghubungi Gemini API, dan menghasilkan balasan suara (Text-to-Speech) menggunakan `edge-tts`.

Berikut adalah langkah-langkah untuk menjalankan sistem ini:

## Prasyarat
1. **Perangkat Keras:** ESP32-S3 yang sudah terhubung dengan modul MAX98357A (Speaker), INMP441 (Mikrofon), dan OLED I2C.
2. **Perangkat Lunak PC:** Python 3.9+ dan VS Code (dengan ekstensi PlatformIO atau Arduino IDE).
3. **Jaringan:** Pastikan PC/Laptop dan ESP32 Anda terhubung ke **jaringan WiFi yang sama**.

---

## Langkah 1: Persiapan PC (Python Backend)

Bagian ini bertanggung jawab untuk memproses suara Anda dan memberikan balasan AI.

1. Buka terminal/Command Prompt di PC Anda.
2. Cek IP Address lokal PC Anda:
   - **Windows:** Jalankan perintah `ipconfig` (cari bagian `IPv4 Address`).
   - **Mac/Linux:** Jalankan perintah `ifconfig` atau `ip a`.
   - *Contoh: IP Anda mungkin `192.168.0.14` atau `192.168.1.5`*
3. Buka folder backend dan instal library yang dibutuhkan:
   ```bash
   cd ~/PROJECT/ESP32-S3/BRAFI_AI_Backend
   pip install -r requirements.txt
   ```
4. Jalankan server FastAPI:
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000
   ```
   *Biarkan terminal ini tetap terbuka. Server akan standby di port `8000`.*

---

## Langkah 2: Konfigurasi ESP32

Sebelum mem-flash program, Anda harus memberi tahu ESP32 kemana ia harus mengirim data suara (ke IP PC Anda).

1. Buka file `BRAFI_AI/config.h` di editor Anda.
2. Sesuaikan pengaturan jaringan dengan WiFi rumah/kantor Anda:
   ```cpp
   #define WIFI_SSID       "NamaWiFiAnda"
   #define WIFI_PASSWORD   "PasswordWiFiAnda"
   ```
3. Ubah `DEFAULT_BACKEND_URL` dengan memasukkan IP Address PC Anda (dari Langkah 1):
   ```cpp
   // Ganti dengan IP PC/Laptop Anda
   #define DEFAULT_BACKEND_URL "http://192.168.1.50:8000" 
   ```

*(Opsional)* Jika IP PC Anda sering berubah, Anda dapat memodifikasi URL ini tanpa perlu flash ulang kode dengan cara masuk ke **Web Dashboard** BRAFI AI (menggunakan browser di PC/HP ke alamat IP ESP32) dan mengubahnya di menu **Settings**.

---

## Langkah 3: Build dan Flash ESP32

1. Buka folder proyek ESP32 di terminal:
   ```bash
   cd ~/PROJECT/ESP32-S3/BRAFI_AI
   ```
2. Lakukan kompilasi dan flash kode ke ESP32. Jika Anda menggunakan ESP-IDF:
   ```bash
   idf.py build flash monitor
   ```
   *(Atau klik tombol "Upload" jika Anda menggunakan Arduino IDE / PlatformIO).*
3. Tunggu hingga proses flash selesai dan ESP32 restart.

---

## Langkah 4: Pengujian & Penggunaan

1. Pantau Serial Monitor. Anda akan melihat ESP32 mencoba terhubung ke WiFi.
2. Setelah terhubung, layar OLED pada perangkat akan menampilkan status **"Ready"** atau indikator standby.
3. Bicaralah pada mikrofon. Saat Anda berhenti berbicara, ESP32 akan mencetak log:
   `Mengirim Audio ke Backend http://<IP-PC-ANDA>:8000...`
4. Di terminal backend (PC) Anda, akan terlihat request audio masuk dan diproses oleh Gemini AI.
5. Beberapa detik kemudian, ESP32 akan memutar suara balasan dari AI melalui speaker!

> [!TIP]
> Jika tidak ada suara yang keluar, pastikan pin speaker MAX98357A: **BCLK=15, LRC=16, DIN=7**. Mic INMP441: **SCK=14, WS=4, SD=13** (WS mic jangan ke GPIO 15).
