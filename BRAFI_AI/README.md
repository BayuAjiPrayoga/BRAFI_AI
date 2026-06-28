# BRAFI AI Assistant

Ini adalah tahap pertama dari pengembangan BRAFI AI Assistant berbasis ESP32-S3. Pada tahap ini, sistem menampilkan layar boot di OLED dan menyapa pengguna melalui speaker.

## 📌 Fitur
- Animasi loading saat ESP32-S3 dihidupkan.
- Memutar suara sapaan (file `.wav`) dari memori internal (SPIFFS).
- Layar OLED menampilkan status `READY` setelah suara selesai.

## 🔌 Wiring (Pinout)

### OLED SSD1306 (I2C)
| OLED Pin | ESP32-S3 Pin | Keterangan |
|----------|--------------|------------|
| VCC      | 3.3V         | Power      |
| GND      | GND          | Ground     |
| SDA      | GPIO 8       | Data I2C   |
| SCL      | GPIO 9       | Clock I2C  |

### MAX98357A (I2S Amplifier)
| MAX98357A Pin | ESP32-S3 Pin | Keterangan |
|---------------|--------------|------------|
| VIN           | 5V / 3.3V    | Power      |
| GND           | GND          | Ground     |
| DIN           | GPIO 7       | Data In    |
| BCLK          | GPIO 15      | Bit Clock  |
| LRC           | GPIO 16      | LR Clock   |
| SD            | 3.3V         | Enable     |

### INMP441 (I2S Microphone)
| INMP441 Pin | ESP32-S3 Pin | Keterangan |
|-------------|--------------|------------|
| VDD         | 3.3V         | Power      |
| GND         | GND          | Ground     |
| SCK         | GPIO 14      | Bit Clock  |
| WS          | GPIO 4       | Word Select (bukan 15!) |
| SD          | GPIO 13      | Data Out   |
| L/R         | GND          | Mono (left) |

**Catatan:** Hubungkan pin `SPK+` dan `SPK-` dari modul ke Speaker (4Ω 3W).

## 🛠️ Persiapan & Kompilasi

### 1. Kebutuhan Library
Pastikan Anda telah menginstal library berikut via **Library Manager** di Arduino IDE:
- `Adafruit GFX Library`
- `Adafruit SSD1306`

### 2. File Audio (welcome.wav)
Sistem membutuhkan file suara berformat **WAV (16-bit, Mono, 16kHz)**.
- Simpan file tersebut dengan nama `welcome.wav` di dalam folder `data/` project ini.

### 3. Cara Upload SPIFFS (Filesystem)
1. Buka project ini di Arduino IDE.
2. Gunakan plugin **ESP32 Sketch Data Upload** (Atau `LittleFS/SPIFFS data upload` untuk ESP32 versi 3.x).
3. Klik menu **Tools -> ESP32 Sketch Data Upload**.
4. Tunggu hingga proses upload partisi data selesai.

### 4. Cara Upload Kode
1. Pilih Board: **ESP32S3 Dev Module**.
2. Pastikan setting *Partition Scheme* memiliki space untuk SPIFFS (Contoh: `Default 4MB with spiffs` atau yang lain).
3. Klik tombol **Upload**.

## 🎵 Mengganti File Audio
1. Siapkan file `.wav` baru. Pastikan menggunakan konfigurasi:
   - Format: PCM
   - Sample Rate: 16000 Hz
   - Channels: 1 (Mono)
   - Resolution: 16-bit
2. Beri nama `welcome.wav` dan timpa file lama di folder `data/`.
3. Lakukan upload SPIFFS ulang.
