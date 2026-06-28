import os
import uuid
import asyncio
import base64
import time
import json
import glob
import datetime
import subprocess
import aiohttp
import pytz
import paho.mqtt.client as mqtt
from fastapi import FastAPI, File, UploadFile, Form, Request, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
from gtts import gTTS
from faster_whisper import WhisperModel
from typing import List

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

os.makedirs("public/audio", exist_ok=True)
os.makedirs("temp", exist_ok=True)

app.mount("/audio", StaticFiles(directory="public/audio"), name="audio")

# --- Load Environment Variables ---
load_dotenv()

# --- Gemini Configuration ---
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "")
GEMINI_URL = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={GEMINI_API_KEY}"

# --- Fase 4: Dual LLM Router ---
ACTIVE_LLM = "gemini"  # Default fallback

# DeepSeek / Aivene Configuration
DEEPSEEK_API_KEY = os.getenv("DEEPSEEK_API_KEY", "")  
DEEPSEEK_URL = "https://api.aivene.com/v1/chat/completions"

# OpenRouter Configuration
OPENROUTER_API_KEY = os.getenv("OPENROUTER_API_KEY", "")
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
OPENROUTER_MODEL = "openai/gpt-oss-120b:free"

async def call_gemini(messages: list, system_prompt: str, http_session: aiohttp.ClientSession) -> tuple:
    """Panggil Gemini API. Return (reply_text, usage_metadata)"""
    payload = {
        "system_instruction": {"parts": [{"text": system_prompt}]},
        "contents": messages
    }
    async with http_session.post(GEMINI_URL, json=payload) as resp:
        if resp.status != 200:
            err_text = await resp.text()
            raise Exception(f"Gemini HTTP {resp.status}: {err_text}")
        data = await resp.json()
        if 'candidates' in data and len(data['candidates']) > 0:
            reply = data['candidates'][0]['content']['parts'][0]['text']
            usage = data.get('usageMetadata', {})
            return reply, usage
        elif 'error' in data:
            raise Exception(f"Gemini error: {data['error'].get('message', str(data['error']))}")
        else:
            raise Exception("Gemini: no candidates in response")

async def call_deepseek(messages: list, system_prompt: str, http_session: aiohttp.ClientSession) -> tuple:
    """Panggil DeepSeek API (OpenAI-compatible). Return (reply_text, usage_metadata)"""
    # Convert Gemini message format ke OpenAI format
    openai_messages = [{"role": "system", "content": system_prompt}]
    for msg in messages:
        role = "user" if msg.get("role") == "user" else "assistant"
        text = msg.get("parts", [{}])[0].get("text", "")
        if text:
            openai_messages.append({"role": role, "content": text})
    
    headers = {
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        "Content-Type": "application/json"
    }
    payload = {
        "model": "deepseek-v4-flash",
        "messages": openai_messages,
        "max_tokens": 500
    }
    async with http_session.post(DEEPSEEK_URL, json=payload, headers=headers) as resp:
        if resp.status != 200:
            err_text = await resp.text()
            raise Exception(f"DeepSeek HTTP {resp.status}: {err_text}")
        data = await resp.json()
        if 'choices' in data and len(data['choices']) > 0:
            reply = data['choices'][0]['message']['content']
            usage = data.get('usage', {})
            # Convert OpenAI usage format ke Gemini-like format
            usage_meta = {
                "promptTokenCount": usage.get("prompt_tokens", 0),
                "candidatesTokenCount": usage.get("completion_tokens", 0),
                "totalTokenCount": usage.get("total_tokens", 0)
            }
            return reply, usage_meta
        else:
            raise Exception(f"DeepSeek error: {data.get('error', {}).get('message', str(data))}")

async def call_openrouter(messages: list, system_prompt: str, http_session: aiohttp.ClientSession) -> tuple:
    """Panggil OpenRouter API. Return (reply_text, usage_metadata)"""
    openai_messages = [{"role": "system", "content": system_prompt}]
    for msg in messages:
        role = "user" if msg.get("role") == "user" else "assistant"
        text = msg.get("parts", [{}])[0].get("text", "")
        if text:
            openai_messages.append({"role": role, "content": text})
            
    headers = {
        "Authorization": f"Bearer {OPENROUTER_API_KEY}",
        "Content-Type": "application/json",
        "HTTP-Referer": "http://localhost",
        "X-Title": "BRAFI AI"
    }
    payload = {
        "model": OPENROUTER_MODEL,
        "messages": openai_messages,
        "max_tokens": 500
    }
    async with http_session.post(OPENROUTER_URL, json=payload, headers=headers) as resp:
        if resp.status != 200:
            err_text = await resp.text()
            raise Exception(f"OpenRouter HTTP {resp.status}: {err_text}")
        data = await resp.json()
        if 'choices' in data and len(data['choices']) > 0:
            reply = data['choices'][0]['message']['content']
            usage = data.get('usage', {})
            usage_meta = {
                "promptTokenCount": usage.get("prompt_tokens", 0),
                "candidatesTokenCount": usage.get("completion_tokens", 0),
                "totalTokenCount": usage.get("total_tokens", 0)
            }
            return reply, usage_meta
        else:
            raise Exception(f"OpenRouter error: {data.get('error', {}).get('message', str(data))}")

async def call_llm(messages: list, system_prompt: str, http_session: aiohttp.ClientSession) -> tuple:
    """
    LLM Router. Returns: (reply_text, llm_source, usage_metadata)
    Coba LLM utama dulu, fallback jika error 429/quota.
    """
    if ACTIVE_LLM == "gemini" or ACTIVE_LLM == "auto":
        try:
            reply, usage = await call_gemini(messages, system_prompt, http_session)
            return reply, "gemini", usage
        except Exception as e:
            if ACTIVE_LLM == "auto" and ("429" in str(e) or "quota" in str(e).lower()):
                print(f"Gemini rate limit, fallback ke DeepSeek: {e}")
            elif ACTIVE_LLM == "gemini":
                raise e
    
    if ACTIVE_LLM == "deepseek" or ACTIVE_LLM == "auto":
        try:
            reply, usage = await call_deepseek(messages, system_prompt, http_session)
            return reply, "deepseek", usage
        except Exception as e:
            if ACTIVE_LLM == "auto":
                print(f"DeepSeek gagal, fallback ke OpenRouter: {e}")
            elif ACTIVE_LLM == "deepseek":
                raise Exception(f"DeepSeek gagal: {e}")
                
    if ACTIVE_LLM == "openrouter" or ACTIVE_LLM == "auto":
        try:
            reply, usage = await call_openrouter(messages, system_prompt, http_session)
            return reply, "openrouter", usage
        except Exception as e:
            raise Exception(f"OpenRouter gagal: {e}")
    
    raise Exception(f"LLM '{ACTIVE_LLM}' gagal atau tidak dikenali")

# --- Fase 1a: Faster-Whisper Model (di-load sekali saat startup) ---
whisper_model = None

# --- Fase 1b: FAQ Cache (jawaban offline tanpa API) ---
FAQ_CACHE = {
    # Identitas & Umum
    "siapa kamu": "Saya BRAFI AI, asisten pintar berbasis ESP32 buatan Kelompok Josjis.",
    "nama kamu": "Nama saya BRAFI AI.",
    "nama": "Nama saya BRAFI AI.",
    "kamu siapa": "Saya BRAFI AI, asisten pintar berbasis ESP32.",
    "kamu buatan siapa": "Saya diciptakan oleh Kelompok Josjis, mahasiswa cerdas dari Teknik Informatika UTB.",
    "siapa pembuatmu": "Saya dibuat oleh Kelompok Josjis dari kampus UTB.",
    "berapa umurmu": "Sebagai kecerdasan buatan, saya tidak memiliki umur secara biologis.",
    "kamu bisa apa": "Saya bisa mengobrol santai, menjawab pertanyaan seputar kampus, dan membantu tugas Anda.",
    "dimana rumahmu": "Rumah saya ada di dalam sirkuit ESP32 ini.",
    
    # Sapaan & Kesopanan
    "halo": "Halo! Ada yang bisa saya bantu hari ini?",
    "hai": "Hai! Ada yang bisa saya bantu?",
    "apa kabar": "Kabar saya sangat baik! Bagaimana dengan Anda?",
    "terima kasih": "Sama-sama! Senang bisa membantu Anda.",
    "makasih": "Sama-sama! Jangan ragu untuk bertanya lagi.",
    "selamat pagi": "Selamat pagi! Semoga hari Anda menyenangkan. Ada yang bisa dibantu?",
    "selamat siang": "Selamat siang! Tetap semangat ya. Ada yang bisa dibantu?",
    "selamat sore": "Selamat sore! Waktunya bersantai setelah beraktivitas.",
    "selamat malam": "Selamat malam! Selamat beristirahat.",
    "sampai jumpa": "Sampai jumpa lagi! Hati-hati di jalan.",
    "dah": "Dadah! Sampai bertemu lagi.",
    
    # Kuliah & Kampus (UTB / Informatika)
    "kampus utb": "UTB adalah Universitas Teknologi Bandung, kampus yang keren dan inovatif!",
    "apa itu utb": "UTB adalah Universitas Teknologi Bandung, tempat bernaungnya mahasiswa-mahasiswa hebat.",
    "apa itu informatika": "Informatika adalah jurusan yang mempelajari ilmu komputer, pemrograman, dan teknologi masa depan. Sangat menantang dan seru!",
    "anak it": "Anak IT itu keren, jago ngoding, dan masa depannya cerah!",
    "kelompok josjis": "Kelompok Josjis adalah tim pengembang luar biasa yang menciptakan saya.",
    "siapa dosenmu": "Dosen penguji saya tentunya bapak atau ibu dosen Teknik Informatika UTB yang luar biasa.",
    "tugas uas": "Tugas UAS ini pastinya akan mendapat nilai A! Amin.",
    "lagi ngapain": "Saya sedang menunggu pertanyaan dari Anda.",
    "pusing nugas": "Jangan menyerah! Tarik napas panjang, minum air putih, dan selesaikan pelan-pelan. Anda pasti bisa!",
    "kapan wisuda": "Semoga secepatnya ya! Terus semangat kerjakan tugas akhir dan skripsinya.",
}

def cek_faq(teks: str) -> str | None:
    teks_lower = teks.lower().strip()
    teks_lower = teks_lower.replace("?", "").replace("!", "").replace(".", "")
    for kunci, jawaban in FAQ_CACHE.items():
        if kunci in teks_lower:
            return jawaban
    return None

# (glob sudah di-import di atas)
def cleanup_old_files(directory="public/audio", max_files=10):
    """Menghapus file audio lama agar penyimpanan server tidak penuh."""
    try:
        files = glob.glob(os.path.join(directory, "*.*"))
        if len(files) > max_files:
            files.sort(key=os.path.getmtime)
            for f in files[:-max_files]:
                os.remove(f)
    except Exception as e:
        print(f"Cleanup error: {e}")

# --- Fase 1c: Token Tracking ---
TOKEN_FILE = "tokens.json"

token_stats = {
    "session_prompt": 0,
    "session_reply": 0,
    "session_total": 0,
    "all_time_total": {
        "gemini": 0,
        "deepseek": 0,
        "openrouter": 0
    },
    "last_request_prompt": 0,
    "last_request_reply": 0,
    "last_request_total": 0
}

# Load from file if exists
if os.path.exists(TOKEN_FILE):
    try:
        with open(TOKEN_FILE, "r") as f:
            saved_stats = json.load(f)
            if "all_time_total" in saved_stats and isinstance(saved_stats["all_time_total"], dict):
                for k, v in saved_stats["all_time_total"].items():
                    token_stats["all_time_total"][k] = v
            if "active_llm" in saved_stats:
                ACTIVE_LLM = saved_stats["active_llm"]
    except Exception as e:
        print(f"Error loading tokens: {e}")

def update_token_stats(usage_metadata: dict, llm_name: str):
    """Parse usageMetadata dari API response dan update akumulasi serta simpan ke disk."""
    prompt_tokens = usage_metadata.get("promptTokenCount", 0)
    reply_tokens = usage_metadata.get("candidatesTokenCount", 0)
    total_tokens = usage_metadata.get("totalTokenCount", 0)
    
    token_stats["last_request_prompt"] = prompt_tokens
    token_stats["last_request_reply"] = reply_tokens
    token_stats["last_request_total"] = total_tokens
    token_stats["session_prompt"] += prompt_tokens
    token_stats["session_reply"] += reply_tokens
    token_stats["session_total"] += total_tokens
    
    # Tambah ke total per model
    if llm_name not in token_stats["all_time_total"]:
        token_stats["all_time_total"][llm_name] = 0
    token_stats["all_time_total"][llm_name] += total_tokens
    
    # Save to file
    try:
        with open(TOKEN_FILE, "w") as f:
            json.dump({
                "all_time_total": token_stats["all_time_total"],
                "active_llm": ACTIVE_LLM
            }, f)
    except Exception as e:
        print(f"Error saving tokens: {e}")

# --- Fase 1d: Conversation History per Session ---
conversation_history = {}  # key: session_id, value: {"messages": [...], "last_access": timestamp}
MAX_HISTORY_TURNS = 3 # Diturunkan dari 10 ke 3 untuk sangat menghemat token
MAX_SESSIONS = 20  # Batasi jumlah sesi aktif agar tidak memory leak

def cleanup_old_sessions():
    """Hapus sesi percakapan yang sudah lama tidak aktif (>30 menit)."""
    if len(conversation_history) <= MAX_SESSIONS:
        return
    now = time.time()
    expired = [sid for sid, data in conversation_history.items() if now - data.get("last_access", 0) > 1800]
    for sid in expired:
        del conversation_history[sid]
    # Jika masih terlalu banyak, hapus yang paling lama
    if len(conversation_history) > MAX_SESSIONS:
        sorted_sessions = sorted(conversation_history.items(), key=lambda x: x[1].get("last_access", 0))
        for sid, _ in sorted_sessions[:len(conversation_history) - MAX_SESSIONS]:
            del conversation_history[sid]

def get_history(session_id: str) -> list:
    session = conversation_history.get(session_id)
    if session:
        session["last_access"] = time.time()
        return session.get("messages", [])
    return []

def add_to_history(session_id: str, role: str, text: str):
    if session_id not in conversation_history:
        conversation_history[session_id] = {"messages": [], "last_access": time.time()}
    conversation_history[session_id]["last_access"] = time.time()
    conversation_history[session_id]["messages"].append({"role": role, "parts": [{"text": text}]})
    # Trim jika lebih dari MAX_HISTORY_TURNS * 2 pesan
    if len(conversation_history[session_id]["messages"]) > MAX_HISTORY_TURNS * 2:
        conversation_history[session_id]["messages"] = conversation_history[session_id]["messages"][-MAX_HISTORY_TURNS * 2:]
    # Bersihkan sesi lama secara periodik
    cleanup_old_sessions()


# --- Fase 2a: MQTT Client ---
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

def mqtt_publish(topic: str, payload):
    try:
        mqtt_client.publish(f"brafi/{topic}", str(payload), qos=0, retain=False)
    except Exception as e:
        print(f"MQTT publish error: {e}")

# --- Fase 3a: WebSocket Connection Manager ---
class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        for connection in self.active_connections:
            try:
                await connection.send_text(json.dumps(message))
            except:
                pass

ws_manager = ConnectionManager()

# --- Startup Event: Load Whisper Model + Connect MQTT ---
@app.on_event("startup")
async def startup_event():
    global whisper_model
    print("Loading Faster-Whisper model 'base'...")
    whisper_model = WhisperModel("base", device="cpu", compute_type="int8")
    print("Faster-Whisper model loaded!")
    mqtt_connect()


@app.post("/api/chat")
async def chat_endpoint(
    request: Request,
    text: str = Form(None), 
    audio: UploadFile = File(None),
    sys_prompt: str = Form("Kamu adalah asisten pintar bernama BRAFI AI berbahasa Indonesia. PENTING: Selalu jawab dengan sangat singkat, padat, dan maksimal 2 kalimat. Jangan beri penjelasan panjang lebar kecuali diminta."),
    max_reply_chars: int = Form(0)
):
    try:
        start_time = time.time()
        
        # --- TIME INJECTION (Sadar Waktu) ---
        tz = pytz.timezone('Asia/Jakarta')
        current_time = datetime.datetime.now(tz).strftime("%H:%M WIB, %A, %d %B %Y")
        sys_prompt += f" Waktu saat ini: {current_time}."
        
        reply_text = "Maaf, saya tidak menerima input apapun."
        source = "none"
        usage_meta = {}
        transcribed_text = None
        
        # --- Fase 1d: Session ID dari IP client ---
        session_id = request.client.host if request.client else "unknown"
        
        async with aiohttp.ClientSession() as session:
            # JIKA INPUT DARI MIKROFON ESP32 (FILE WAV)
            if audio:
                audio_data = await audio.read()
                
                # --- Fase 1a: Faster-Whisper STT (audio → teks lokal) ---
                whisper_success = False
                temp_wav = None
                if whisper_model:
                    try:
                        temp_wav = f"temp/{uuid.uuid4().hex[:8]}.wav"
                        with open(temp_wav, "wb") as f:
                            f.write(audio_data)
                        
                        segments, info = whisper_model.transcribe(
                            temp_wav, 
                            language="id",
                            condition_on_previous_text=False  # Mencegah halusinasi berulang tanpa membuang suara asli
                        )
                        transcribed_text = " ".join([seg.text for seg in segments]).strip()
                        
                        # Hapus file temp setelah transcribe
                        if temp_wav and os.path.exists(temp_wav):
                            os.remove(temp_wav)
                        
                        if transcribed_text and len(transcribed_text) > 2:
                            print(f"Whisper STT: '{transcribed_text}'")
                            whisper_success = True
                        else:
                            print("Whisper STT: hasil kosong/terlalu pendek, fallback ke audio Gemini")
                            transcribed_text = None
                    except Exception as whisper_err:
                        print(f"Whisper error: {whisper_err}, fallback ke audio Gemini")
                        transcribed_text = None
                        # Bersihkan temp file jika ada error
                        if temp_wav and os.path.exists(temp_wav):
                            os.remove(temp_wav)
                
                if whisper_success and transcribed_text:
                    # --- Fase 1b: Cek FAQ dulu sebelum panggil Gemini ---
                    faq_answer = cek_faq(transcribed_text)
                    if faq_answer:
                        reply_text = faq_answer
                        source = "faq_cache"
                    else:
                        # --- Fase 4: Kirim teks + history via LLM Router ---
                        history = get_history(session_id)
                        contents = list(history)  # copy history
                        contents.append({"role": "user", "parts": [{"text": transcribed_text + "\n\n(Jawab dengan 1-2 kalimat pendek saja)"}]})
                        
                        try:
                            reply_text, source, usage_meta = await call_llm(contents, sys_prompt, session)
                        except Exception as llm_err:
                            print(f"LLM Router Error (Audio): {llm_err}")
                            if "429" in str(llm_err) or "quota" in str(llm_err).lower():
                                reply_text = "Maaf, model AI sedang kehabisan kuota atau limit token. Silakan ganti model."
                            else:
                                reply_text = f"Maaf, terjadi error pada model AI: {llm_err}"
                            source = "error"
                        
                        # --- Fase 1d: Simpan ke history ---
                        add_to_history(session_id, "user", transcribed_text)
                        add_to_history(session_id, "model", reply_text)
                else:
                    # FALLBACK: Kirim audio base64 langsung ke Gemini (perilaku lama)
                    audio_b64 = base64.b64encode(audio_data).decode('utf-8')
                    
                    payload = {
                        "system_instruction": {"parts": [{"text": sys_prompt}]},
                        "contents": [{
                            "parts": [
                                {"text": "Dengarkan audio ini. Ini adalah percakapan/pertanyaan dari pengguna. JAWABLAH pertanyaan atau balas pernyataan tersebut secara langsung. DILARANG KERAS hanya menulis ulang (transcribe) apa yang diucapkan pengguna. Jawablah dengan singkat dan padat. Jika audio HANYA berisi bising/noise tanpa suara manusia, balas tepat dengan kata '[IGNORE]'."},
                                {"inline_data": {"mime_type": "audio/wav", "data": audio_b64}}
                            ]
                        }]
                    }
                    
                    async with session.post(GEMINI_URL, json=payload) as resp:
                        data = await resp.json()
                        if 'candidates' in data and len(data['candidates']) > 0:
                            reply_text = data['candidates'][0]['content']['parts'][0]['text']
                            source = "gemini"
                            # --- Fase 1c: Parse token usage ---
                            if 'usageMetadata' in data:
                                usage_meta = data['usageMetadata']
                        else:
                            print("Gemini API mengembalikan respons kosong (mungkin difilter). Mengabaikan audio.")
                            reply_text = "[IGNORE]"
                            source = "gemini"
                
            # JIKA INPUT DARI WEB DASHBOARD (TEKS)
            elif text:
                # --- Fase 1b: Cek FAQ dulu sebelum panggil Gemini ---
                faq_answer = cek_faq(text)
                if faq_answer:
                    reply_text = faq_answer
                    source = "faq_cache"
                else:
                    # --- Fase 4: Kirim teks + history via LLM Router ---
                    history = get_history(session_id)
                    contents = list(history)  # copy history
                    contents.append({"role": "user", "parts": [{"text": text + "\n\nJawablah dengan singkat dan padat."}]})
                    
                    try:
                        reply_text, source, usage_meta = await call_llm(contents, sys_prompt, session)
                    except Exception as llm_err:
                        print(f"LLM Router Error (Text): {llm_err}")
                        if "429" in str(llm_err) or "quota" in str(llm_err).lower():
                            reply_text = "Maaf, model AI sedang kehabisan kuota atau limit token. Silakan ganti model di pengaturan."
                        else:
                            reply_text = f"Maaf, terjadi error pada model AI: {llm_err}"
                        source = "error"
                    
                    # --- Fase 1d: Simpan ke history ---
                    add_to_history(session_id, "user", text)
                    add_to_history(session_id, "model", reply_text)

        # --- Fase 1c: Update token stats ---
        if usage_meta:
            update_token_stats(usage_meta, source)
        elif source == "faq_cache":
            # FAQ tidak pakai API, token = 0
            token_stats["last_request_prompt"] = 0
            token_stats["last_request_reply"] = 0
            token_stats["last_request_total"] = 0

        clean_text = reply_text.strip().replace("*", "").replace("#", "")
        
        # --- BUG FIX #2: Skip TTS jika response adalah [IGNORE] ---
        if clean_text == "[IGNORE]":
            elapsed_ms = int((time.time() - start_time) * 1000)
            return JSONResponse(content={
                "status": "success",
                "reply_text": "[IGNORE]",
                "reply_truncated": False,
                "full_text_url": "",
                "audio_url": "",
                "source": source,
                "latency_ms": elapsed_ms,
                "token": {"last_total": 0, "session_total": token_stats["session_total"]}
            })
        
        response_text = clean_text
        reply_truncated = False
        if max_reply_chars and max_reply_chars > 0 and len(clean_text) > max_reply_chars:
            response_text = clean_text[:max_reply_chars].rstrip() + "..."
            reply_truncated = True

        # --- AUDIO CACHING: Lewati TTS jika teks yang sama sudah pernah diproses ---
        import hashlib
        text_hash = hashlib.md5(clean_text.encode('utf-8')).hexdigest()
        audio_filename = f"cache_{text_hash}.wav"
        audio_path = f"public/audio/{audio_filename}"
        
        if not os.path.exists(audio_path):
            # PROSES TTS MENGGUNAKAN gTTS (MP3 sementara)
            temp_mp3 = f"temp/{uuid.uuid4().hex[:8]}.mp3"
            tts = gTTS(text=clean_text, lang='id', slow=False)
            await asyncio.to_thread(tts.save, temp_mp3)
            
            # Konversi ke WAV 16kHz Mono (non-blocking via asyncio)
            await asyncio.to_thread(
                subprocess.run,
                ["ffmpeg", "-i", temp_mp3, "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", "-y", audio_path],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            if os.path.exists(temp_mp3):
                os.remove(temp_mp3)
            print(f"TTS Audio Cache Miss. Generated: {audio_path}")
        else:
            print(f"TTS Audio Cache Hit! ({audio_path})")

        base_url_str = str(request.base_url).rstrip("/")
        audio_url = f"{base_url_str}/audio/{audio_filename}"

        text_filename = f"reply_{uuid.uuid4().hex[:8]}.txt"
        text_path = f"public/audio/{text_filename}"
        with open(text_path, "w", encoding="utf-8") as f:
            f.write(clean_text)
        
        elapsed_ms = int((time.time() - start_time) * 1000)
        
        # --- Fase 2a: MQTT publish setelah request selesai ---
        mqtt_publish("status", "Ready")
        mqtt_publish("token/last_prompt", token_stats["last_request_prompt"])
        mqtt_publish("token/last_reply", token_stats["last_request_reply"])
        mqtt_publish("token/last_total", token_stats["last_request_total"])
        mqtt_publish("token/session_total", token_stats["session_total"])
        mqtt_publish("token/source", source)
        mqtt_publish("reply", clean_text[:200])  # Kirim max 200 char ke MQTT
        mqtt_publish("latency_ms", elapsed_ms)
        
        # --- Fase 3a: WebSocket broadcast setelah request selesai ---
        await ws_manager.broadcast({
            "type": "update",
            "status": "Ready",
            "last_reply": clean_text[:500],
            "token": {
                "last_total": token_stats["last_request_total"],
                "session_total": token_stats["session_total"],
                "all_time_total": token_stats["all_time_total"]
            },
            "source": source,
            "latency_ms": elapsed_ms
        })
        
        # Panggil pembersih file lama agar storage tidak penuh
        cleanup_old_files()
        
        # Response JSON — field lama (status, reply_text, audio_url) TETAP ADA
        # Field baru (source, latency_ms, token) ditambahkan tanpa menghapus yang lama
        return JSONResponse(content={
            "status": "success",
            "reply_text": response_text,
            "reply_truncated": reply_truncated,
            "full_text_url": f"{base_url_str}/{text_path.replace('public/', '')}",
            "audio_url": audio_url,
            "source": source,
            "latency_ms": elapsed_ms,
            "token": {
                "last_total": token_stats["last_request_total"],
                "session_total": token_stats["session_total"]
            }
        })
        
    except Exception as e:
        return JSONResponse(content={"status": "error", "message": str(e)}, status_code=500)

@app.post("/api/transcribe")
async def transcribe_endpoint(audio: UploadFile = File(...)):
    try:
        audio_data = await audio.read()
        audio_b64 = base64.b64encode(audio_data).decode('utf-8')
        
        # Format umum (Bisa M4A, WAV, AMR, WEBM tergantung browser)
        # Gemini 1.5 flash mensupport berbagai format audio
        mime_type = audio.content_type
        if not mime_type or not mime_type.startswith("audio/"):
            mime_type = "audio/mp4" # fallback m4a
            
        payload = {
            "contents": [{
                "parts": [
                    {"text": "Tuliskan ulang dengan persis apa yang dikatakan dalam rekaman suara ini tanpa menambahkan deskripsi atau komentar apapun. Jika kosong atau tidak jelas, jawab [TIDAK JELAS]."},
                    {"inline_data": {"mime_type": mime_type, "data": audio_b64}}
                ]
            }]
        }
        
        async with aiohttp.ClientSession() as session:
            async with session.post(GEMINI_URL, json=payload) as resp:
                data = await resp.json()
                if 'candidates' in data and len(data['candidates']) > 0:
                    text = data['candidates'][0]['content']['parts'][0]['text'].strip()
                    if text == "[TIDAK JELAS]":
                        text = ""
                    return JSONResponse(content={"status": "success", "text": text})
                else:
                    return JSONResponse(content={"status": "error", "message": "Gagal transkripsi"})
    except Exception as e:
        return JSONResponse(content={"status": "error", "message": str(e)}, status_code=500)

# --- Fase 1c: Endpoint Token Stats ---
@app.get("/api/token-stats")
async def get_token_stats():
    return JSONResponse(content=token_stats)

# --- Fase 4: Endpoint Set LLM ---
@app.post("/api/set-llm")
async def set_llm(llm: str = Form(...)):
    global ACTIVE_LLM
    if llm in ["gemini", "deepseek", "openrouter", "auto"]:
        ACTIVE_LLM = llm
        mqtt_publish("llm/active", ACTIVE_LLM)
        
        # Persist to TOKEN_FILE so it survives restarts
        try:
            with open(TOKEN_FILE, "w") as f:
                json.dump({
                    "all_time_total": token_stats["all_time_total"],
                    "active_llm": ACTIVE_LLM
                }, f)
        except Exception as e:
            pass
            
        return JSONResponse(content={"status": "success", "active_llm": ACTIVE_LLM})
    return JSONResponse(content={"status": "error", "message": "LLM tidak valid"}, status_code=400)

@app.get("/api/llm-status")
async def llm_status():
    return JSONResponse(content={"active_llm": ACTIVE_LLM, "token_stats": token_stats})

# --- Fase 3a: WebSocket Endpoint ---
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await ws_manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()  # keep alive
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)

@app.get("/")
def read_root():
    return {"message": "BRAFI AI Backend is running!"}
