# Agent Prompt — BRAFI AI Upgrade v2.0

## Identitas Tugas

Kamu adalah AI coding agent yang bertugas mengupgrade project **BRAFI AI** sesuai plan di file `BRAFI_AI_UPGRADE_PLAN.md`. Baca seluruh plan tersebut sebelum menulis satu baris kode pun.

---

## Konteks Project yang Harus Kamu Pahami Sebelum Mulai

Project ini adalah AI desk assistant berbasis ESP32-S3. Ada **dua bagian terpisah**:

1. **Backend Python** — folder `BRAFI_AI_Backend/`, file utama `main.py`, framework FastAPI
2. **Firmware ESP32** — folder `BRAFI_AI/`, file utama `BRAFI_AI.ino`, bahasa Arduino C++

Keduanya sudah **berjalan dan berfungsi**. Tugasmu adalah **menambahkan fitur**, bukan menulis ulang dari nol.

---

## Aturan Keras — Wajib Dipatuhi Setiap Saat

### Jangan pernah mengubah hal-hal ini tanpa instruksi eksplisit:

```
PIN I2S SPEAKER : BCLK=15, LRC=16, DOUT=7   → JANGAN DIUBAH
PIN I2S MIC     : SCK=14, WS=4, SD=13        → JANGAN DIUBAH
PIN I2C OLED    : SDA=8, SCL=9               → JANGAN DIUBAH
FORMAT AUDIO    : WAV 16kHz mono 16-bit PCM  → JANGAN DIUBAH
ENDPOINT HTTP   : POST /api/chat             → JANGAN DIUBAH
FORMAT RESPONSE : {status, reply_text, audio_url} → FIELD INI HARUS SELALU ADA
```

### Backward compatibility adalah hukum:

- Response JSON dari `main.py` ke ESP32 **harus tetap** mengandung field `status`, `reply_text`, `audio_url`
- Boleh tambah field baru di response, tapi **tidak boleh hapus atau rename** field yang sudah ada
- `gemini.cpp` di firmware sudah parse response JSON tersebut — jika kamu ubah struktur response, firmware akan crash

### Jangan ganti yang sudah benar:

- File `tts.cpp`, `mic.cpp`, `audio.cpp`, `display.cpp`, `filesystem.cpp`, `splash.cpp`, `welcome_wav.h` → **JANGAN DIUBAH** kecuali ada bug yang harus diperbaiki dan disebutkan di plan
- Alokasi audio buffer di ESP32 **harus tetap** pakai `heap_caps_malloc(MALLOC_CAP_SPIRAM)` — jangan pakai `malloc` biasa

---

## Cara Kamu Bekerja

### Langkah wajib sebelum coding:

1. Baca `BRAFI_AI_UPGRADE_PLAN.md` secara keseluruhan
2. Baca file yang akan kamu ubah terlebih dahulu — jangan langsung tulis ulang
3. Identifikasi tepat di baris berapa kamu akan menambahkan kode
4. Kerjakan satu fase dalam satu sesi, jangan loncat-loncat fase

### Langkah saat coding:

1. **Selalu baca file sebelum edit** — gunakan read/view tool sebelum write
2. **Edit secara surgical** — tambahkan kode di titik yang tepat, jangan rewrite seluruh file jika tidak perlu
3. **Setelah selesai satu fase**, verifikasi dengan membaca ulang file yang diubah
4. **Jangan hapus komentar** yang sudah ada di kode original — mereka penting untuk debugging hardware

### Langkah verifikasi:

Setelah setiap fase selesai, cek:
- [ ] File yang tidak seharusnya diubah tetap sama?
- [ ] Format response JSON backend masih ada field `status`, `reply_text`, `audio_url`?
- [ ] Tidak ada import baru yang belum ada di `requirements.txt`?
- [ ] Tidak ada pin yang berubah di kode firmware?

---

## Urutan Fase — Kerjakan Persis Urutan Ini

### FASE 1 — Backend Python saja (file: `main.py`)

Kerjakan dalam urutan ini, satu per satu:

**1a. Faster-Whisper STT**
- Install: `faster-whisper`
- Load model `base` sekali saat startup dengan `@app.on_event("startup")`
- Di endpoint `POST /api/chat`, jika input berupa audio:
  - Tulis audio bytes ke `temp/{uuid}.wav`
  - Transcribe dengan `WhisperModel.transcribe(path, language="id")`
  - Ambil hasilnya sebagai teks
  - Hapus file temp setelah transcribe
  - Kirim **teks** (bukan audio) ke Gemini
- Jika transcribe gagal atau hasilnya kosong → fallback ke perilaku lama (kirim audio base64 ke Gemini)
- Model Faster-Whisper disimpan di variabel global, di-load sekali, tidak per request

**1b. FAQ Cache**
- Tambahkan dict `FAQ_CACHE` persis seperti di plan
- Tambahkan fungsi `cek_faq(teks: str) -> str | None`
- Di alur request: setelah dapat teks (dari Whisper atau dari form), panggil `cek_faq()` dulu
- Jika ada jawaban FAQ → langsung balas, skip panggil Gemini sama sekali
- Catat source sebagai `"faq_cache"` untuk token stats dan MQTT

**1c. Token Tracking**
- Tambahkan dict global `token_stats` persis seperti di plan
- Parse `usageMetadata` dari setiap response Gemini
- Update `token_stats` setiap request yang menggunakan Gemini
- Buat endpoint baru `GET /api/token-stats` yang return `token_stats`
- Jika source adalah `faq_cache`, token tetap 0 (tidak ada API call)

**1d. Conversation History**
- Tambahkan dict global `conversation_history` dan `MAX_HISTORY_TURNS = 10`
- Tambahkan fungsi `get_history()` dan `add_to_history()`
- Gunakan `request.client.host` sebagai `session_id`
- Kirim history sebagai `contents` array ke Gemini (bukan hanya satu pesan)
- Auto-trim jika lebih dari 20 pesan (10 turn)

---

### FASE 2 — MQTT (file: `main.py` dan firmware baru)

**2a. MQTT di Backend Python**
- Install: `paho-mqtt`
- Tambahkan MQTT client, connect di startup
- Tambahkan fungsi `mqtt_publish(topic, payload)`
- Publish ke semua topic yang ada di plan setiap kali request selesai
- Jika MQTT gagal connect → log error tapi backend tetap jalan normal (jangan crash)

**2b. MQTT di Firmware ESP32**
- Buat file baru: `mqtt.h` dan `mqtt.cpp`
- Install library `PubSubClient` (instruksikan user untuk install via Library Manager)
- Implementasi sesuai spesifikasi di plan
- Di `BRAFI_AI.ino`:
  - Tambahkan `#include "mqtt.h"`
  - Tambahkan `initMQTT()` di `setup()` **setelah** `initWiFi()`
  - Tambahkan `mqttLoop()` di dalam `webTask()` **setelah** `server.handleClient()`
  - Tambahkan publish heap setiap 10 detik di `loop()`

---

### FASE 3 — WebSocket (file: `main.py` dan `web.cpp`)

**3a. WebSocket di Backend**
- Tambahkan class `ConnectionManager` dan endpoint `/ws` persis seperti di plan
- Tambahkan `await ws_manager.broadcast(...)` di akhir setiap request yang selesai diproses
- WebSocket broadcast **non-blocking** — jika gagal (browser tutup), tidak boleh crash backend

**3b. WebSocket Client di Web Dashboard**
- File: `web.cpp` di firmware
- Baca file ini dulu, temukan bagian di mana HTML string didefinisikan
- Tambahkan elemen HTML: `ai-status`, `last-reply`, `token-last`, `token-total`, `latency`, `source`
- Tambahkan JavaScript WebSocket client persis seperti di plan
- `backendHost` diambil dari URL backend yang sudah tersimpan di Preferences

---

### FASE 4 — Dual LLM Router (file: `main.py`)

- Tambahkan variabel global `ACTIVE_LLM = "gemini"`
- Refactor panggilan Gemini ke fungsi `call_gemini(messages, system_prompt, session)`
- Tambahkan fungsi `call_deepseek(messages, system_prompt, session)`
  - DeepSeek pakai format OpenAI-compatible: `POST /v1/chat/completions`
  - Header: `Authorization: Bearer {DEEPSEEK_API_KEY}`
- Tambahkan fungsi `call_llm()` sebagai router sesuai plan
- Tambahkan endpoint `POST /api/set-llm` dan `GET /api/llm-status`
- Update web dashboard: tambahkan dropdown pilih LLM

---

### FASE 5 — Security (file: `config.h`, `web.cpp`, `gemini.cpp`, `network.cpp`)

- Hapus WiFi credentials dan API key dari `config.h`
- Tambahkan form input API key di settings page (`web.cpp`)
- Implementasi AES-128-ECB encrypt/decrypt menggunakan `mbedtls`
- Encryption key: `ESP.getEfuseMac()` (MAC address) + hardcoded salt `"BRAFI_AI_2025"`
- Simpan ke Preferences dengan key `"gemini_key_enc"`
- Di `gemini.cpp`: baca dari Preferences → decrypt → gunakan untuk request
- Jangan pernah tampilkan plaintext API key di Serial atau web response

---

### FASE 6 — FreeRTOS Queue (file: `BRAFI_AI.ino`) — KERJAKAN TERAKHIR

- Ini perubahan paling berisiko — kerjakan hanya setelah fase 1-5 verified berjalan
- Ganti `volatile String web_chat_request` dan `web_chat_reply` dengan `QueueHandle_t`
- Ikuti implementasi persis seperti di plan
- Ukuran struct `ChatMessage`: request 512 bytes, reply 1024 bytes
- Test dengan web chat dulu sebelum test voice

---

## Format Output yang Diharapkan per Fase

Setiap kali kamu selesai satu sub-fase, berikan:

```
✅ FASE [X] SELESAI
File yang diubah: [list file]
File yang TIDAK diubah: [konfirmasi]
Perubahan yang dibuat: [ringkasan singkat]
Yang perlu di-test: [instruksi test]
```

---

## Hal-hal yang Sering Bikin Salah — Baca Ini Baik-baik

### Tentang Faster-Whisper:
- Import: `from faster_whisper import WhisperModel` (underscore, bukan dash)
- Transcribe return tuple: `(segments, info)` — iterate `segments` untuk dapat teks
- Cara ambil teks: `" ".join([seg.text for seg in segments]).strip()`
- Model size `"base"` cukup, jangan pakai `"large"` — terlalu berat untuk lokal

### Tentang Gemini API format teks (bukan audio):
```python
# Format yang benar untuk kirim TEKS ke Gemini dengan history
payload = {
    "system_instruction": {"parts": [{"text": sys_prompt}]},
    "contents": [
        # ... history turns ...
        {"role": "user", "parts": [{"text": teks_dari_whisper}]}
    ]
}
```

### Tentang MQTT dengan FastAPI async:
- `mqtt_client.loop_start()` jalan di thread terpisah — aman dengan asyncio
- Jangan pakai `mqtt_client.loop_forever()` — itu blocking
- Publish dari async function: langsung panggil `mqtt_client.publish()` — sync call, tapi cukup cepat

### Tentang PubSubClient di ESP32:
- `client.loop()` harus dipanggil setiap iterasi task, bukan hanya saat ada pesan
- Jika MQTT disconnect, `client.connected()` return false — cek ini di setiap iterasi dan reconnect
- Callback `on_message` dipanggil dari dalam `client.loop()` — jangan panggil `client.loop()` dari dalam callback

### Tentang WebSocket FastAPI:
- `await websocket.receive_text()` di while loop adalah cara keep-alive yang benar
- Jika client disconnect, `WebSocketDisconnect` exception dilempar — catch ini untuk cleanup
- Broadcast ke semua client: iterate list dan try/except per client — jika satu client error, jangan stop

### Tentang AES di ESP32 mbedtls:
- Include: `#include "mbedtls/aes.h"`
- AES-128-ECB butuh key 16 bytes persis
- Plaintext harus di-pad ke kelipatan 16 bytes (PKCS7 padding)
- Simpan ciphertext sebagai hex string di Preferences (bukan raw bytes)

---

## Kapan Harus Berhenti dan Tanya

Berhenti dan minta konfirmasi dari user jika:

1. Kamu menemukan kode di file yang **berbeda dari yang ada di plan** dan tidak yakin cara integrasi yang benar
2. Kamu akan mengubah file yang **tidak ada di daftar "file yang diubah"** di plan
3. Response JSON yang akan kamu ubah bisa mempengaruhi firmware ESP32
4. Ada dependency versi yang konflik di `requirements.txt`
5. Kamu tidak menemukan titik penyisipan kode yang tepat di file yang ada

**Jangan** berasumsi dan jalan terus jika ada ambiguitas yang bisa merusak firmware yang sudah berjalan.

---

## Perintah Mulai

Mulai dengan membaca file-file ini secara berurutan:
1. `BRAFI_AI_UPGRADE_PLAN.md` — baca keseluruhan
2. `BRAFI_AI_Backend/main.py` — baca keseluruhan
3. `BRAFI_AI_Backend/requirements.txt`
4. `BRAFI_AI/BRAFI_AI.ino`
5. `BRAFI_AI/gemini.cpp`
6. `BRAFI_AI/web.cpp`

Setelah membaca semua file di atas, konfirmasi bahwa kamu sudah paham dengan menyebutkan:
- Berapa endpoint HTTP yang ada saat ini di `main.py`
- Di baris berapa kamu akan menambahkan Faster-Whisper transcribe di endpoint `/api/chat`
- Nama variabel yang akan kamu ganti di `BRAFI_AI.ino` untuk FreeRTOS queue (Fase 6)

Baru setelah konfirmasi tersebut, mulai eksekusi **Fase 1a**.
