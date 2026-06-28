# BRAFI AI — Project Upgrade Plan
**Versi:** 2.0  
**Author:** Bayu Aji Prayoga  
**Board:** ESP32-S3-N16R8  
**Status project saat ini:** Working prototype dengan HTTP + Gemini multimodal audio

---

## Konteks Project

BRAFI AI adalah AI desk assistant berbasis ESP32-S3 dengan komponen:
- **Hardware:** ESP32-S3 DevKit, OLED SSD1306 (I2C GPIO 8/9), MAX98357A speaker (I2S port 0: BCLK=15 LRC=16 DOUT=7), INMP441 mic (I2S port 1: SCK=14 WS=4 SD=13)
- **Firmware:** Arduino C++ (BRAFI_AI.ino + modul terpisah: mic, audio, tts, gemini, web, display, network, filesystem)
- **Backend:** Python FastAPI (`main.py`), berjalan di PC/server lokal
- **Komunikasi ESP32 ↔ Backend:** HTTP POST ke `http://192.168.x.x:8000`
- **LLM:** Gemini 2.5 Flash via REST API (Google Generative Language)
- **TTS:** gTTS → ffmpeg → WAV 16kHz mono, disimpan di `public/audio/`, diambil ESP32 via HTTP GET
- **Web dashboard:** Diserve dari ESP32 (WebServer library), ada chat dan settings

---

## Masalah Utama yang Akan Diselesaikan

1. **Token Gemini boros** — audio WAV dikirim sebagai base64 langsung ke Gemini (audio multimodal), padahal teks jauh lebih hemat
2. **Tidak ada fallback** saat Gemini rate limit (error 429)
3. **Web dashboard tidak realtime** — harus refresh untuk lihat status AI
4. **Tidak ada monitoring token usage**
5. **Tidak ada MQTT** (requirement UAS)
6. **API key hardcoded** di `config.h` (security lemah)
7. **Komunikasi antar FreeRTOS core** memakai `volatile String` yang tidak thread-safe

---

## Arsitektur Target

```
[INMP441 Mic]
     ↓ I2S
[ESP32-S3]
     ↓ HTTP POST (audio WAV)          ← komunikasi utama, UAS requirement
[Backend Python FastAPI]
     ├─ Faster-Whisper (STT lokal)    ← audio → teks
     ├─ FAQ Cache (offline answers)   ← pertanyaan dasar tanpa API
     ├─ LLM Router
     │    ├─ Gemini 2.5 Flash         ← utama
     │    └─ DeepSeek                 ← fallback saat Gemini 429/error
     ├─ parse usage_metadata          ← hitung token
     ├─ MQTT publish                  ← brafi/token/*, brafi/status
     ├─ WebSocket broadcast           ← web dashboard realtime
     └─ HTTP response → ESP32         ← teks reply + audio_url
          ↓ HTTP GET (download WAV)
[MAX98357A Speaker]
     ↑ I2S
[ESP32-S3]
```

**Protokol per jalur:**
| Jalur | Protokol | Keterangan |
|---|---|---|
| ESP32 → Backend (audio/teks) | HTTP POST multipart | Tidak berubah, UAS requirement |
| ESP32 ← Backend (reply) | HTTP response JSON | Tidak berubah |
| ESP32 ← Backend (audio) | HTTP GET WAV | Tidak berubah |
| Backend → Web browser (live) | WebSocket | Baru, untuk dashboard realtime |
| Backend → MQTT broker | MQTT publish | Baru, UAS requirement + monitoring |
| MQTT broker → ESP32 | MQTT subscribe | Baru, command dari luar |

---

## Fase 1 — Pisahkan STT + Token Tracking + FAQ Cache

**Target:** Kurangi konsumsi token Gemini minimal 70-80%  
**File yang diubah:** `main.py` (backend Python)  
**File yang TIDAK diubah:** Semua file firmware ESP32 (`.ino`, `.cpp`, `.h`)

### 1.1 Integrasi Faster-Whisper (STT lokal)

**Instalasi:**
```bash
pip install faster-whisper
```

**Model yang dipakai:** `base` (cukup akurat untuk Bahasa Indonesia, ringan)  
**Bahasa:** `id` (Indonesia)

**Logika di `main.py`:**
```
Terima audio WAV dari ESP32
  ↓
Faster-Whisper transcribe(audio_bytes, language="id")
  ↓
Dapat teks transkrip
  ↓
Kirim TEKS (bukan audio) ke Gemini
```

**Catatan penting:**
- Audio sementara ditulis ke file di folder `temp/` untuk Faster-Whisper, lalu dihapus setelah transcribe
- Faster-Whisper bisa terima bytes langsung dengan cara write ke BytesIO atau temp file
- Model `base` di-load sekali saat startup, tidak setiap request
- Jika Faster-Whisper gagal (confidence rendah / kosong), fallback kirim audio langsung ke Gemini seperti sekarang

### 1.2 Token Tracking

Gemini API mengembalikan `usage_metadata` di setiap response:
```json
{
  "usageMetadata": {
    "promptTokenCount": 45,
    "candidatesTokenCount": 120,
    "totalTokenCount": 165
  }
}
```

**Yang harus dilakukan di `main.py`:**
- Parse `usageMetadata` dari response Gemini
- Simpan akumulasi di variabel global Python:
  ```python
  token_stats = {
      "session_prompt": 0,
      "session_reply": 0,
      "session_total": 0,
      "all_time_total": 0,
      "last_request_prompt": 0,
      "last_request_reply": 0,
      "last_request_total": 0
  }
  ```
- Expose via endpoint `GET /api/token-stats`
- Publish via MQTT setiap request (Fase 2)

### 1.3 FAQ Cache (Offline Answers)

**Logika:** Sebelum panggil API apapun, cek apakah pertanyaan cocok dengan FAQ lokal.

**Dict FAQ di `main.py`:**
```python
FAQ_CACHE = {
    "siapa kamu": "Saya BRAFI AI, asisten pintar berbasis ESP32-S3 buatan Bayu Aji Prayoga.",
    "nama kamu": "Nama saya BRAFI AI.",
    "nama": "Nama saya BRAFI AI.",
    "kamu siapa": "Saya BRAFI AI, asisten pintar berbasis ESP32-S3.",
    "kamu buatan siapa": "Saya dibuat oleh Bayu Aji Prayoga, mahasiswa Informatika UTB.",
    "halo": "Halo! Ada yang bisa saya bantu?",
    "hai": "Hai! Ada yang bisa saya bantu?",
    "apa kabar": "Saya baik-baik saja, terima kasih sudah bertanya! Ada yang bisa saya bantu?",
    "terima kasih": "Sama-sama! Ada lagi yang bisa saya bantu?",
    "selamat pagi": "Selamat pagi! Ada yang bisa saya bantu hari ini?",
    "selamat siang": "Selamat siang! Ada yang bisa saya bantu?",
    "selamat malam": "Selamat malam! Ada yang bisa saya bantu?",
    "berapa umurmu": "Saya adalah AI, saya tidak memiliki umur.",
    "kamu bisa apa": "Saya bisa menjawab pertanyaan, membantu percakapan, dan memberikan informasi yang kamu butuhkan.",
}

def cek_faq(teks: str) -> str | None:
    teks_lower = teks.lower().strip()
    teks_lower = teks_lower.replace("?", "").replace("!", "").replace(".", "")
    for kunci, jawaban in FAQ_CACHE.items():
        if kunci in teks_lower:
            return jawaban
    return None
```

**Alur dengan FAQ:**
```
Terima teks dari Faster-Whisper
  ↓
cek_faq(teks)
  ├─ Ada jawaban → langsung balas, skip API Gemini/DeepSeek
  └─ Tidak ada → lanjut ke LLM Router
```

### 1.4 Conversation History per Session

**Logika:**
- Setiap ESP32 (diidentifikasi by IP atau device_id) punya history sendiri
- History disimpan di dict Python in-memory
- Auto-trim: maksimal 10 turn (20 pesan: 10 user + 10 assistant)
- History dikirim ke Gemini sebagai `contents` array

```python
conversation_history = {}  # key: session_id, value: list of messages

MAX_HISTORY_TURNS = 10

def get_history(session_id: str) -> list:
    return conversation_history.get(session_id, [])

def add_to_history(session_id: str, role: str, text: str):
    if session_id not in conversation_history:
        conversation_history[session_id] = []
    conversation_history[session_id].append({"role": role, "parts": [{"text": text}]})
    # Trim jika lebih dari MAX_HISTORY_TURNS * 2 pesan
    if len(conversation_history[session_id]) > MAX_HISTORY_TURNS * 2:
        conversation_history[session_id] = conversation_history[session_id][-MAX_HISTORY_TURNS * 2:]
```

**session_id:** Gunakan IP address klien (`request.client.host`) sebagai session_id default.

---

## Fase 2 — MQTT Monitoring

**Target:** Penuhi requirement UAS MQTT, monitoring token & status realtime  
**File yang diubah:** `main.py`  
**Library:** `paho-mqtt`

**Instalasi:**
```bash
pip install paho-mqtt
```

**MQTT Broker:** Gunakan broker lokal (Mosquitto) atau broker publik `broker.hivemq.com` port 1883 untuk testing.

### 2.1 Topics yang Di-publish Backend

| Topic | Payload | Keterangan |
|---|---|---|
| `brafi/status` | `"Listening"` / `"Thinking"` / `"Speaking"` / `"Ready"` | Status AI saat ini |
| `brafi/token/last_prompt` | angka integer | Token prompt request terakhir |
| `brafi/token/last_reply` | angka integer | Token reply request terakhir |
| `brafi/token/last_total` | angka integer | Token total request terakhir |
| `brafi/token/session_total` | angka integer | Akumulasi token sejak backend start |
| `brafi/token/source` | `"gemini"` / `"deepseek"` / `"faq_cache"` / `"whisper_only"` | Sumber jawaban |
| `brafi/reply` | teks string | Teks balasan AI terakhir |
| `brafi/latency_ms` | angka integer | Waktu total proses dalam ms |
| `brafi/heap` | angka integer | Sisa heap ESP32 (dikirim ESP32, bukan backend) |

### 2.2 Topics yang Di-subscribe ESP32

| Topic | Payload | Aksi ESP32 |
|---|---|---|
| `brafi/command/speak` | teks string | ESP32 TTS-kan teks yang diterima |
| `brafi/command/reset_wifi` | apapun | ESP32 hapus kredensial WiFi dan restart |
| `brafi/command/reboot` | apapun | ESP32 restart |

### 2.3 MQTT Client di Backend Python

```python
import paho.mqtt.client as mqtt

MQTT_BROKER = "broker.hivemq.com"  # ganti ke "localhost" jika pakai Mosquitto lokal
MQTT_PORT = 1883
MQTT_CLIENT_ID = "brafi_backend"

mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID)

def mqtt_connect():
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
        print(f"MQTT connected to {MQTT_BROKER}")
    except Exception as e:
        print(f"MQTT connection failed: {e}")

def mqtt_publish(topic: str, payload: str):
    try:
        mqtt_client.publish(f"brafi/{topic}", payload, qos=0, retain=False)
    except Exception as e:
        print(f"MQTT publish error: {e}")
```

**Panggil `mqtt_connect()` saat FastAPI startup:**
```python
@app.on_event("startup")
async def startup_event():
    load_whisper_model()  # load Faster-Whisper
    mqtt_connect()
```

### 2.4 MQTT Client di ESP32 (Firmware)

**Library:** `PubSubClient` (install via Arduino Library Manager)

**File baru yang dibuat:** `mqtt.h` dan `mqtt.cpp`

**`mqtt.h`:**
```cpp
#ifndef MQTT_H
#define MQTT_H
#include <Arduino.h>

void initMQTT();
void mqttLoop();
void mqttPublish(const char* topic, const char* payload);
void mqttPublishHeap();

#endif
```

**`mqtt.cpp` — logika:**
- Gunakan library `PubSubClient`
- Broker sama dengan yang dipakai backend
- Connect saat WiFi sudah tersambung
- `mqttLoop()` dipanggil di `webTask()` setiap iterasi
- Subscribe `brafi/command/speak`, `brafi/command/reset_wifi`, `brafi/command/reboot`
- Callback untuk setiap command:
  - `speak` → panggil `playTTS(payload)`
  - `reset_wifi` → clear Preferences dan `ESP.restart()`
  - `reboot` → `ESP.restart()`
- `mqttPublishHeap()` → publish `ESP.getFreeHeap()` ke `brafi/heap`
- Auto-reconnect jika koneksi terputus

**Pin MQTT di `BRAFI_AI.ino`:**
- Tambahkan `mqttLoop()` di dalam `webTask()` setelah `server.handleClient()`
- Tambahkan `initMQTT()` di `setup()` setelah `initWiFi()`
- Tambahkan `mqttPublishHeap()` di `loop()` setiap 10 detik

---

## Fase 3 — WebSocket untuk Web Dashboard Realtime

**Target:** Web dashboard bisa lihat status AI, token counter, dan reply terbaru secara realtime tanpa polling  
**File yang diubah:** `main.py` (backend)  
**File yang diubah:** HTML/JS di web dashboard ESP32 (`web.cpp`)

**Catatan penting:** WebSocket HANYA antara Browser ↔ Backend Python. ESP32 TIDAK memakai WebSocket. ESP32 tetap HTTP.

### 3.1 WebSocket di Backend Python

**Library:** FastAPI sudah include WebSocket support bawaan.

```python
from fastapi import WebSocket, WebSocketDisconnect
from typing import List

# Manager untuk semua koneksi WebSocket aktif
class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        import json
        for connection in self.active_connections:
            try:
                await connection.send_text(json.dumps(message))
            except:
                pass

ws_manager = ConnectionManager()

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await ws_manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()  # keep alive
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)
```

**Broadcast dilakukan setiap kali ada request selesai diproses:**
```python
await ws_manager.broadcast({
    "type": "update",
    "status": "Ready",
    "last_reply": reply_text,
    "token": {
        "last_total": token_stats["last_request_total"],
        "session_total": token_stats["session_total"]
    },
    "source": "gemini",
    "latency_ms": elapsed_ms
})
```

### 3.2 Update Web Dashboard di ESP32

**File:** `web.cpp` — bagian HTML yang diinjek ke browser

**Tambahkan JavaScript WebSocket client:**
```javascript
const ws = new WebSocket('ws://' + backendHost + '/ws');

ws.onmessage = function(event) {
    const data = JSON.parse(event.data);
    if (data.type === 'update') {
        document.getElementById('ai-status').textContent = data.status;
        document.getElementById('last-reply').textContent = data.last_reply;
        document.getElementById('token-last').textContent = data.token.last_total;
        document.getElementById('token-total').textContent = data.token.session_total;
        document.getElementById('latency').textContent = data.latency_ms + ' ms';
        document.getElementById('source').textContent = data.source;
    }
};

ws.onclose = function() {
    setTimeout(() => location.reload(), 3000); // reconnect dengan reload
};
```

**`backendHost`** = ambil dari settings yang sudah tersimpan di Preferences ESP32 (backend URL).

---

## Fase 4 — Dual LLM Router (Gemini + DeepSeek)

**Target:** Gemini sebagai LLM utama, DeepSeek sebagai fallback otomatis saat Gemini error 429 atau timeout  
**File yang diubah:** `main.py`

### 4.1 LLM Router Logic

```python
ACTIVE_LLM = "gemini"  # bisa diubah via endpoint atau web UI

GEMINI_URL = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={GEMINI_API_KEY}"
DEEPSEEK_URL = "https://api.deepseek.com/v1/chat/completions"  # sesuaikan dengan endpoint DeepSeek kamu

async def call_llm(messages: list, system_prompt: str, session: aiohttp.ClientSession) -> tuple[str, str, dict]:
    """
    Returns: (reply_text, llm_source, usage_metadata)
    llm_source: "gemini" atau "deepseek"
    """
    # Coba Gemini dulu
    if ACTIVE_LLM == "gemini" or ACTIVE_LLM == "auto":
        try:
            reply, usage = await call_gemini(messages, system_prompt, session)
            return reply, "gemini", usage
        except Exception as e:
            if "429" in str(e) or "quota" in str(e).lower():
                print("Gemini rate limit, fallback ke DeepSeek")
            else:
                raise e
    
    # Fallback ke DeepSeek
    reply, usage = await call_deepseek(messages, system_prompt, session)
    return reply, "deepseek", usage
```

### 4.2 Endpoint untuk Ganti LLM Manual

```python
@app.post("/api/set-llm")
async def set_llm(llm: str = Form(...)):
    global ACTIVE_LLM
    if llm in ["gemini", "deepseek", "auto"]:
        ACTIVE_LLM = llm
        return {"status": "success", "active_llm": ACTIVE_LLM}
    return {"status": "error", "message": "LLM tidak valid"}

@app.get("/api/llm-status")
async def llm_status():
    return {"active_llm": ACTIVE_LLM, "token_stats": token_stats}
```

### 4.3 Update Web Dashboard

Tambahkan di HTML web dashboard ESP32:
- Dropdown `<select>` untuk pilih LLM: `Gemini (utama)`, `DeepSeek`, `Auto (fallback)`
- Badge warna untuk LLM yang aktif saat ini
- Counter token realtime dari WebSocket

---

## Fase 5 — Security (AES Enkripsi API Key)

**Target:** API key tidak hardcoded di `config.h`, disimpan terenkripsi di Preferences ESP32  
**File yang diubah:** `config.h`, `network.cpp`, `web.cpp`  
**Library Arduino:** `mbedtls` (sudah built-in di ESP32 Arduino Core)

### 5.1 Alur Security

```
User input API key via web dashboard
  ↓
AES-128-ECB encrypt (key = device MAC address + salt)
  ↓
Simpan ciphertext di Preferences ESP32 ("api_key_enc")
  ↓
Saat digunakan: baca dari Preferences → AES decrypt → gunakan
  ↓
TIDAK PERNAH tampilkan plaintext API key di Serial Monitor atau web
```

### 5.2 Enkripsi API Key

**Di `web.cpp`:** Tambahkan form input API key di settings page  
**Di firmware:** Saat user submit API key baru → encrypt → simpan  
**Di `gemini.cpp`:** Saat kirim request → baca Preferences → decrypt → gunakan

**Encryption key:** Kombinasi ESP32 MAC address + hardcoded salt (bukan key yang bisa ditebak)

### 5.3 Hapus dari config.h

```cpp
// config.h SEBELUM — BERBAHAYA
#define WIFI_SSID       "Lantai 3"
#define WIFI_PASSWORD   "Hayanghitut17"

// config.h SESUDAH — AMAN
// WiFi credentials disimpan di Preferences, tidak di source code
// API key disimpan terenkripsi di Preferences
// config.h hanya berisi PIN definitions dan konstanta non-rahasia
```

---

## Fase 6 — FreeRTOS Thread Safety

**Target:** Komunikasi antar Core 0 (web task) dan Core 1 (AI loop) yang benar  
**File yang diubah:** `BRAFI_AI.ino`

### 6.1 Masalah Saat Ini

```cpp
// BERBAHAYA — String tidak thread-safe lintas core
volatile bool web_chat_pending = false;
String web_chat_request = "";   // ← bisa corrupt saat Core 0 write, Core 1 read
String web_chat_reply = "";     // ← sama
```

### 6.2 Solusi: FreeRTOS Queue

```cpp
// Ganti dengan queue
struct ChatMessage {
    char request[512];
    char reply[1024];
};

QueueHandle_t chatRequestQueue;   // Core 0 → Core 1 (request)
QueueHandle_t chatReplyQueue;     // Core 1 → Core 0 (reply)

// Di setup():
chatRequestQueue = xQueueCreate(1, sizeof(ChatMessage));
chatReplyQueue   = xQueueCreate(1, sizeof(ChatMessage));

// Core 0 (webTask) — kirim request ke Core 1:
ChatMessage msg;
strncpy(msg.request, userInput.c_str(), sizeof(msg.request)-1);
xQueueSend(chatRequestQueue, &msg, portMAX_DELAY);

// Core 1 (loop) — terima request dari Core 0:
ChatMessage msg;
if (xQueueReceive(chatRequestQueue, &msg, 0) == pdTRUE) {
    String reply = askGeminiText(String(msg.request));
    strncpy(msg.reply, reply.c_str(), sizeof(msg.reply)-1);
    xQueueSend(chatReplyQueue, &msg, 0);
}
```

---

## Struktur File Setelah Upgrade

### Backend Python

```
BRAFI_AI_Backend/
├── main.py                  ← diupdate (STT, history, token, MQTT, WS, LLM router)
├── requirements.txt         ← diupdate (tambah faster-whisper, paho-mqtt)
├── whisper_model/           ← model Faster-Whisper (auto-download pertama kali)
├── public/audio/            ← file WAV TTS (tidak berubah)
└── temp/                    ← file audio sementara (tidak berubah)
```

### Firmware ESP32

```
BRAFI_AI/
├── BRAFI_AI.ino             ← diupdate (queue, initMQTT, mqttPublishHeap)
├── config.h                 ← diupdate (hapus WiFi/API credentials)
├── gemini.cpp / .h          ← diupdate (decrypt API key, history support)
├── tts.cpp / .h             ← tidak berubah
├── mic.cpp / .h             ← tidak berubah
├── audio.cpp / .h           ← tidak berubah
├── display.cpp / .h         ← tidak berubah
├── network.cpp / .h         ← diupdate (handle API key dari Preferences)
├── web.cpp / .h             ← diupdate (WebSocket JS client, LLM dropdown, token UI)
├── mqtt.cpp / .h            ← BARU
├── filesystem.cpp / .h      ← tidak berubah
├── splash.cpp / .h          ← tidak berubah
└── welcome_wav.h            ← tidak berubah
```

---

## Requirements.txt Final

```
fastapi==0.104.1
uvicorn==0.24.0
python-multipart==0.0.6
aiohttp==3.9.0
gtts==2.3.2
paho-mqtt==1.6.1
faster-whisper==1.0.3
```

---

## Urutan Implementasi (Direkomendasikan)

1. **Fase 1.1** — Integrasi Faster-Whisper di `main.py` + test transcribe
2. **Fase 1.3** — Tambah FAQ Cache di `main.py`
3. **Fase 1.2** — Parse dan simpan token stats, buat endpoint `/api/token-stats`
4. **Fase 1.4** — Tambah conversation history
5. **Fase 2** — MQTT di backend Python dulu, test publish via MQTT Explorer
6. **Fase 2.4** — MQTT client di ESP32 firmware
7. **Fase 3** — WebSocket di backend + update HTML di `web.cpp`
8. **Fase 4** — LLM Router (Gemini + DeepSeek)
9. **Fase 5** — Security enkripsi API key
10. **Fase 6** — FreeRTOS queue (lakukan paling akhir, paling berisiko untuk stabilitas)

---

## Constraints & Batasan Penting

### Yang TIDAK boleh diubah tanpa alasan kuat:
- Pin I2S speaker: BCLK=15, LRC=16, DOUT=7
- Pin I2S mic: SCK=14, WS=4, SD=13
- Pin I2C OLED: SDA=8, SCL=9
- Format audio WAV: 16kHz, mono, 16-bit PCM
- Endpoint HTTP utama: `POST /api/chat` (ESP32 sudah hardcode ini)
- Format response JSON: harus tetap ada `status`, `reply_text`, `audio_url`

### Backward compatibility ESP32:
- Response JSON dari backend harus tetap kompatibel dengan `gemini.cpp` yang sudah ada
- Jika ada field baru ditambahkan ke response, pastikan `gemini.cpp` tidak crash jika field tidak ada

### Memory ESP32:
- PSRAM tersedia (N16R8 = 16MB flash, 8MB PSRAM)
- Audio buffer selalu alokasi ke PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- Stack webTask: 8192 bytes — jangan tambah banyak logika di webTask

### API Gemini:
- Model: `gemini-2.5-flash` (bukan gemini-pro atau yang lain)
- Endpoint: `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent`
- Format request untuk teks: `contents[].parts[].text`
- Format response token: `usageMetadata.promptTokenCount`, `candidatesTokenCount`, `totalTokenCount`

---

## Checklist UAS

| Poin UAS | Status Target | Cara Penuhi |
|---|---|---|
| ESP32 | ✅ Sudah | Hardware ESP32-S3 |
| HTTP | ✅ Sudah | POST audio/teks ke backend |
| MQTT | ✅ Fase 2 | Publish token/status, subscribe command |
| Tampilan Web/Mobile | ✅ Sudah | Web dashboard di ESP32 |
| RTOS | ✅ Sudah | FreeRTOS webTask Core 0 |
| Machine Learning | ⚠️ Partial | Faster-Whisper STT lokal + Gemini cloud |
| LLM (Gemini) | ✅ Sudah | Gemini 2.5 Flash |
| Security | ✅ Fase 5 | AES enkripsi API key di Preferences |
| Robotik | ❌ Skip | Tidak diimplementasi |
