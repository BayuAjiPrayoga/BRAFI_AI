# Arsitektur Baru BRAFI AI (Python Backend)

Proyek ini telah direstrukturisasi secara masif untuk mengatasi limitasi dari HTTPClient ESP32 dan Google Translate TTS yang sering gagal. Sekarang, seluruh beban kerja "Otak" AI dipindahkan ke PC lokal Anda, sehingga ESP32 hanya berfungsi sebagai *Mic* dan *Speaker* saja.

## Perubahan yang Dilakukan

1. **Folder `BRAFI_AI_Backend` Baru**
   - Sebuah folder backend telah dibuat di `PROJECT/ESP32-S3/BRAFI_AI_Backend`.
   - Backend ini dibangun dengan `FastAPI`, menggunakan `google-generativeai` untuk merespons suara, dan `edge-tts` (Suara Gadis Indonesia dari Microsoft Azure) untuk mengubah teks ke MP3.
   
2. **ESP32: `config.h` & `web.cpp` Diperbarui**
   - Menambahkan fitur **IP Lokal Dinamis**. Default IP diatur ke `192.168.0.14:8000`, tapi jika IP laptop Anda berubah saat pindah WiFi, Anda bisa masuk ke Mode Setup (Tahan tombol BOOT atau sambungkan ke WiFi setup) dan memasukkan IP Backend yang baru.
   - Anda juga dapat mengganti URL Backend langsung dari modal **Settings** di Web Dashboard.

3. **ESP32: `gemini.cpp` Dirombak Total**
   - Kode SSL dan JSON yang berat untuk Gemini API telah dihapus.
   - Sekarang, fungsi ini hanya meng-upload rekaman suara ke API lokal Anda `http://<IP-ANDA>:8000/api/chat`.
   
4. **ESP32: `tts.cpp` Dirombak Total**
   - Tidak ada lagi koneksi ke `translate.google.com`.
   - Fungsi TTS sekarang memutar URL audio lokal (misal: `http://192.168.0.14:8000/audio/reply_xxx.mp3`) menggunakan `ESP8266Audio`. Hal ini menjamin koneksi stabil dan suara super jernih tanpa batas (limit) harian!

## Cara Menjalankan

> [!IMPORTANT]
> Ikuti urutan ini dengan teliti!

### Langkah 1: Menjalankan Backend Python
Buka terminal baru di PC Anda, lalu jalankan:
```bash
cd ~/PROJECT/ESP32-S3/BRAFI_AI_Backend
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```
*(Server backend akan berjalan di port 8000 dan menunggu suara dari ESP32).*

### Langkah 2: Flash Ulang ESP32
Kembali ke folder `BRAFI_AI`, compile dan flash ulang kode ini ke ESP32 Anda. 
```bash
idf.py build flash monitor
```
*(Atau gunakan PlatformIO / Arduino IDE sesuai yang biasa Anda pakai).*

### Langkah 3: Pengujian
- Katakan sesuatu ke mikrofon ESP32.
- Lihat Serial Monitor, ESP32 akan mencetak `Mengirim Audio ke Backend http://192.168.0.14:8000`.
- Di terminal Python PC Anda, Anda akan melihat request masuk, dan secara ajaib suara jernih dari `edge-tts` akan terdengar di speaker Anda!
