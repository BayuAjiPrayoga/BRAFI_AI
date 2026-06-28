#include "web.h"
#include "config.h"
#include <Preferences.h>
#include "gemini.h"
#include "tts.h"
#include "mbedtls/aes.h"
#include <esp_system.h>
#include <WiFi.h>

WebServer server(80);
extern Preferences preferences;

extern const char* current_ai_status; // Didefinisikan di BRAFI_AI.ino

// --- Fase 6: Extern Queue declarations (struct ChatMessage di config.h) ---
extern QueueHandle_t chatRequestQueue;
extern QueueHandle_t chatReplyQueue;

#include "face_engine.h"
extern FaceEngine* faceEngine;

String current_backend_url = "";

// --- Fase 5: AES Encryption Helpers ---
static void derive_aes_key(uint8_t* key16) {
    // Kombinasi MAC address + salt untuk membuat AES key 16 bytes
    uint64_t mac = ESP.getEfuseMac();
    const char* salt = AES_SALT;
    uint8_t raw[24];
    memcpy(raw, &mac, 8);
    memcpy(raw + 8, salt, strlen(salt) > 16 ? 16 : strlen(salt));
    // Simple hash: XOR fold ke 16 bytes
    memset(key16, 0, 16);
    for (int i = 0; i < 24; i++) {
        key16[i % 16] ^= raw[i];
    }
}

static String aes_encrypt_to_hex(const String& plaintext) {
    uint8_t key[16];
    derive_aes_key(key);
    
    // PKCS7 padding ke kelipatan 16
    int plainLen = plaintext.length();
    int padLen = 16 - (plainLen % 16);
    int totalLen = plainLen + padLen;
    uint8_t* padded = new uint8_t[totalLen];
    memcpy(padded, plaintext.c_str(), plainLen);
    for (int i = plainLen; i < totalLen; i++) padded[i] = padLen;
    
    // AES-128-ECB encrypt
    uint8_t* encrypted = new uint8_t[totalLen];
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (int i = 0; i < totalLen; i += 16) {
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, padded + i, encrypted + i);
    }
    mbedtls_aes_free(&ctx);
    
    // Convert ke hex string
    String hex = "";
    for (int i = 0; i < totalLen; i++) {
        char buf[3];
        sprintf(buf, "%02x", encrypted[i]);
        hex += buf;
    }
    
    delete[] padded;
    delete[] encrypted;
    return hex;
}

String aes_decrypt_from_hex(const String& hexStr) {
    uint8_t key[16];
    derive_aes_key(key);
    
    int encLen = hexStr.length() / 2;
    if (encLen == 0 || encLen % 16 != 0) return "";
    
    uint8_t* encrypted = new uint8_t[encLen];
    for (int i = 0; i < encLen; i++) {
        String byteStr = hexStr.substring(i * 2, i * 2 + 2);
        encrypted[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    }
    
    // AES-128-ECB decrypt
    uint8_t* decrypted = new uint8_t[encLen];
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    for (int i = 0; i < encLen; i += 16) {
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, encrypted + i, decrypted + i);
    }
    mbedtls_aes_free(&ctx);
    
    // Remove PKCS7 padding
    int padVal = decrypted[encLen - 1];
    int dataLen = encLen - padVal;
    if (padVal < 1 || padVal > 16 || dataLen < 0) dataLen = encLen;
    
    String result = "";
    for (int i = 0; i < dataLen; i++) {
        result += (char)decrypted[i];
    }
    
    delete[] encrypted;
    delete[] decrypted;
    return result;
}

const char* setup_html PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>BRAFI AI Setup</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');
        body { font-family: 'Inter', sans-serif; background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%); color: #f8fafc; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .card { background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(20px); padding: 2.5rem; border-radius: 24px; box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5); text-align: center; border: 1px solid rgba(255,255,255,0.08); width: 85%; max-width: 400px; }
        .card h2 { margin-top: 0; font-weight: 700; font-size: 1.8rem; background: linear-gradient(90deg, #60a5fa, #a78bfa); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .card p { color: #94a3b8; font-size: 0.95rem; margin-bottom: 1.5rem; }
        input, select { box-sizing: border-box; width: 100%; padding: 14px; margin: 10px 0; border-radius: 12px; border: 1px solid rgba(255,255,255,0.1); background: rgba(15, 23, 42, 0.6); color: white; outline: none; font-size: 0.95rem; transition: 0.3s; }
        input:focus, select:focus { border-color: #3b82f6; box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.2); }
        button { width: 100%; padding: 14px; background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%); color: white; border: none; border-radius: 12px; cursor: pointer; font-weight: 600; font-size: 1rem; transition: 0.3s; margin-top: 15px; box-shadow: 0 4px 15px rgba(37, 99, 235, 0.3); }
        button:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(37, 99, 235, 0.4); }
    </style>
</head>
<body>
    <div class="card">
        <h2>WiFi Setup</h2>
        <p>Hubungkan BRAFI AI ke Internet</p>
        <form action="/save" method="POST">
            <select name="ssid" id="ssid_select">
                <option value="">Scanning WiFi...</option>
            </select>
            <input type="password" name="pass" placeholder="Password WiFi" required>
            <input type="text" name="backend" placeholder="IP Backend (cth: 192.168.0.14)" required>
            <button type="submit">Connect & Reboot</button>
        </form>
    </div>
    <script>
        fetch('/api/scan').then(r => r.json()).then(ssids => {
            let sel = document.getElementById('ssid_select');
            sel.innerHTML = '';
            if (ssids.length === 0) {
                sel.innerHTML = '<option value="">Tidak ada WiFi ditemukan</option>';
            } else {
                let uniqueSsids = [...new Set(ssids)];
                uniqueSsids.forEach(ssid => {
                    let opt = document.createElement('option');
                    opt.value = ssid;
                    opt.textContent = ssid;
                    sel.appendChild(opt);
                });
            }
        }).catch(err => {
            document.getElementById('ssid_select').innerHTML = '<option value="">Gagal scan WiFi. Refresh halaman.</option>';
        });
    </script>
</body>
</html>
)=====";

const char* dashboard_html PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
    <title>BRAFI AI Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">
    <style>
        html, body { height: 100%; margin: 0; padding: 0; overflow: hidden; }
        body { font-family: 'Inter', sans-serif; background: #f0f4f8; color: #1e293b; display: flex; flex-direction: column; position: absolute; top: 0; bottom: 0; left: 0; right: 0; }
        
        /* Blue Hero Header */
        .hero-header { background: linear-gradient(135deg, #1d4ed8 0%, #3b82f6 100%); padding: 25px 20px 80px; color: white; border-bottom-left-radius: 30px; border-bottom-right-radius: 30px; position: relative; flex-shrink: 0; }
        .hero-top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        .spacer { width: 36px; flex-shrink: 0; }
        .hero-title { font-size: 0.8rem; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; opacity: 0.9; text-align: center; flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding: 0 10px; }
        .hero-settings { background: white; border: none; color: #1d4ed8; border-radius: 50%; width: 40px; height: 40px; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: 0.3s; flex-shrink: 0; box-shadow: 0 4px 10px rgba(0,0,0,0.1); font-size: 1.2rem; }
        .hero-settings:hover { background: rgba(255,255,255,0.3); transform: scale(1.05); }
        .greeting { display: flex; align-items: center; gap: 15px; }
        .greeting-text { min-width: 0; flex: 1; }
        .greeting-text h2 { margin: 0; font-size: 1.5rem; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .greeting-text p { margin: 0; font-size: 0.85rem; opacity: 0.8; margin-top: 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .avatar { width: 48px; height: 48px; border-radius: 50%; background: white; color: #1d4ed8; display: flex; align-items: center; justify-content: center; font-weight: 800; font-size: 1.3rem; box-shadow: 0 4px 10px rgba(0,0,0,0.2); flex-shrink: 0; }

        /* Floating Status Card */
        .status-card { background: white; margin: -50px 16px 16px; border-radius: 20px; padding: 16px; box-shadow: 0 10px 30px rgba(59, 130, 246, 0.15); display: flex; justify-content: space-between; position: relative; z-index: 10; flex-shrink: 0; }
        .stat-col { text-align: center; flex: 1; border-right: 1px solid #f1f5f9; min-width: 0; overflow: hidden; }
        .stat-col:last-child { border-right: none; }
        .stat-icon { font-size: 1.3rem; margin-bottom: 6px; display: inline-block; padding: 8px; border-radius: 14px; background: #f0f4f8; }
        .stat-col:nth-child(1) .stat-icon { color: #3b82f6; background: #eff6ff; }
        .stat-col:nth-child(2) .stat-icon { color: #f59e0b; background: #fef3c7; }
        .stat-col:nth-child(3) .stat-icon { color: #10b981; background: #ecfdf5; }
        .stat-value { font-size: 1rem; font-weight: 700; color: #0f172a; margin-bottom: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding: 0 2px; }
        .stat-label { font-size: 0.65rem; color: #64748b; font-weight: 600; text-transform: uppercase; white-space: nowrap; }

        /* Small Source Widget */
        .source-widget { margin: 0 16px 12px; padding: 12px 16px; background: white; border-radius: 16px; display: flex; justify-content: space-between; align-items: center; box-shadow: 0 4px 15px rgba(0,0,0,0.03); flex-shrink: 0; font-size: 0.8rem; font-weight: 600; }
        .source-widget .label { color: #64748b; }
        .badge { padding: 4px 10px; border-radius: 12px; font-size: 0.7rem; font-weight: 700; letter-spacing: 0.5px; text-transform: uppercase; white-space: nowrap; }
        .badge-gemini { background: #eff6ff; color: #3b82f6; }
        .badge-deepseek { background: #faf5ff; color: #a855f7; }
        .badge-faq_cache { background: #ecfdf5; color: #10b981; }
        .badge-openrouter { background: #fef3c7; color: #f59e0b; }
        .badge-none { background: #f1f5f9; color: #64748b; }

        /* Chat History Section */
        .chat-section { flex: 1; min-height: 0; background: white; margin: 0 16px 12px; border-radius: 20px; box-shadow: 0 4px 15px rgba(0,0,0,0.03); display: flex; flex-direction: column; overflow: hidden; }
        .chat-header { padding: 14px 16px 8px; font-size: 0.85rem; font-weight: 700; color: #0f172a; border-bottom: 1px solid #f8fafc; flex-shrink: 0; }
        .chat-container { flex: 1; padding: 14px 16px; overflow-y: auto; display: flex; flex-direction: column; gap: 10px; scroll-behavior: smooth; }
        
        .bubble { max-width: 85%; padding: 10px 14px; border-radius: 16px; font-size: 0.85rem; line-height: 1.4; animation: popIn 0.3s ease-out; word-wrap: break-word; }
        .user { align-self: flex-end; background: #3b82f6; color: white; border-bottom-right-radius: 4px; box-shadow: 0 4px 10px rgba(59, 130, 246, 0.2); }
        .ai { align-self: flex-start; background: #f1f5f9; color: #1e293b; border-bottom-left-radius: 4px; }
        
        /* Input Area */
        .input-area { padding: 10px 16px 16px; background: #f0f4f8; display: flex; align-items: center; gap: 10px; flex-shrink: 0; z-index: 50; }
        .input-wrapper { flex: 1; position: relative; display: flex; align-items: center; background: white; border-radius: 24px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); }
        input[type="text"] { width: 100%; padding: 14px 16px; padding-right: 50px; border-radius: 24px; border: none; background: transparent; color: #0f172a; outline: none; font-size: 0.9rem; }
        button.send-btn { position: absolute; right: 4px; width: 38px; height: 38px; border-radius: 50%; border: none; background: #3b82f6; color: white; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: 0.3s; box-shadow: 0 4px 10px rgba(59, 130, 246, 0.3); }
        button.send-btn:hover { background: #1d4ed8; transform: scale(1.05); }
        
        /* Modal Settings */
        .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(15, 23, 42, 0.5); backdrop-filter: blur(5px); justify-content: center; align-items: center; z-index: 100; }
        .modal-content { background: white; padding: 25px; border-radius: 24px; width: 85%; max-width: 400px; box-shadow: 0 20px 40px rgba(0,0,0,0.1); max-height: 90vh; overflow-y: auto; }
        .modal-content h3 { margin-top: 0; font-weight: 700; color: #0f172a; margin-bottom: 20px; font-size: 1.1rem; }
        .modal-content label { display: block; margin-top: 12px; margin-bottom: 6px; font-size: 0.8rem; color: #64748b; font-weight: 600; }
        .modal-content input, .modal-content textarea, .modal-content select { width: 100%; box-sizing: border-box; padding: 12px; border-radius: 12px; border: 1px solid #e2e8f0; background: #f8fafc; color: #0f172a; outline: none; transition: 0.3s; font-family: 'Inter', sans-serif; font-size:0.9rem; }
        .modal-content input:focus, .modal-content textarea:focus, .modal-content select:focus { border-color: #3b82f6; background: white; }
        .modal-content textarea { height: 80px; resize: none; }
        .modal-buttons { margin-top: 20px; display: flex; gap: 10px; justify-content: flex-end; }
        .modal-buttons button { padding: 10px 20px; border-radius: 12px; border: none; cursor: pointer; font-weight: 600; font-size: 0.9rem; transition: 0.3s; }
        .btn-cancel { background: #f1f5f9; color: #64748b; }
        .btn-save { background: #3b82f6; color: white; }
        
        @keyframes popIn { 0% { opacity: 0; transform: translateY(10px); } 100% { opacity: 1; transform: translateY(0); } }
    </style>
</head>
<body>
    <div class="hero-header">
        <div class="hero-top">
            <div class="spacer"></div>
            <div class="hero-title" id="ai-title">BRAFI DASHBOARD</div>
            <button class="hero-settings" onclick="openSettings()">⚙️</button>
        </div>
        <div class="greeting">
            <div class="avatar">AI</div>
            <div class="greeting-text">
                <h2 id="greeting-msg">Hello!</h2>
                <p>Welcome to AI Monitoring Panel</p>
            </div>
        </div>
    </div>

    <!-- 3-Column Floating Status Card -->
    <div class="status-card">
        <div class="stat-col">
            <div class="stat-icon">🤖</div>
            <div class="stat-value" id="ai-status">Ready</div>
            <div class="stat-label">System</div>
        </div>
        <div class="stat-col">
            <div class="stat-icon">⚡</div>
            <div class="stat-value" id="latency">-</div>
            <div class="stat-label">Latency</div>
        </div>
        <div class="stat-col">
            <div class="stat-icon">🪙</div>
            <div class="stat-value" id="token-last">0</div>
            <div class="stat-label" id="token-label">Tokens</div>
        </div>
    </div>

    <!-- Source Widget -->
    <div class="source-widget">
        <span class="label">Brain Source:</span>
        <span class="badge badge-none" id="source">None</span>
    </div>

    <div class="chat-section">
        <div class="chat-header">Activity Log</div>
        <div class="chat-container" id="chat">
            <div class="bubble ai">Halo! Sistem monitoring berjalan dengan baik. Silakan berikan perintah teks.</div>
        </div>
    </div>

    <div class="input-area">
        <div class="input-wrapper">
            <input type="text" id="msg" placeholder="Type a message..." onkeypress="if(event.keyCode==13) sendMsg()">
            <button class="send-btn" onclick="sendMsg()">➔</button>
        </div>
    </div>

    <!-- Modal Settings -->
    <div class="modal" id="settingsModal">
        <div class="modal-content">
            <h3>⚙️ Settings</h3>
            <label>AI Name</label>
            <input type="text" id="ai_name" placeholder="Misal: JARVIS">
            
            <label>System Prompt</label>
            <textarea id="sys_prompt" placeholder="Misal: Kamu adalah asisten pintar..."></textarea>
            
            <label>Python Backend URL</label>
            <input type="text" id="backend_url" placeholder="http://192.168.0.14:8000">
            
            <label>LLM Engine</label>
            <select id="llm_select">
                <option value="gemini">Gemini 2.5 Flash</option>
                <option value="deepseek">DeepSeek</option>
                <option value="openrouter">OpenRouter (120B Free)</option>
                <option value="auto">Auto (Fallback)</option>
            </select>
            
            <label>API Key (Optional)</label>
            <input type="password" id="api_key" placeholder="Enter new API key...">
            
            <hr style="border:0; border-top:1px solid #e2e8f0; margin: 20px 0;">
            <h4 style="margin:0; font-weight:700; color:#0f172a;">Face Sculpting</h4>
            
            <label>Eye Width: <span id="val_ew">12</span></label>
            <input type="range" id="face_ew" min="4" max="24" value="12" oninput="document.getElementById('val_ew').innerText=this.value">
            
            <label>Eye Height: <span id="val_eh">16</span></label>
            <input type="range" id="face_eh" min="4" max="30" value="16" oninput="document.getElementById('val_eh').innerText=this.value">
            
            <label>Eye Spacing: <span id="val_esp">16</span></label>
            <input type="range" id="face_esp" min="8" max="40" value="16" oninput="document.getElementById('val_esp').innerText=this.value">
            
            <label>Mouth Width: <span id="val_mw">14</span></label>
            <input type="range" id="face_mw" min="4" max="40" value="14" oninput="document.getElementById('val_mw').innerText=this.value">
            
            <div class="modal-buttons">
                <button class="btn-cancel" onclick="closeSettings()">Cancel</button>
                <button class="btn-save" onclick="saveSettings()">Save</button>
            </div>
        </div>
    </div>

    <script>
        function updateGreeting() {
            const hour = new Date().getHours();
            let greet = "Good Evening";
            if(hour < 12) greet = "Good Morning";
            else if(hour < 18) greet = "Good Afternoon";
            document.getElementById('greeting-msg').textContent = greet + "!";
        }
        updateGreeting();

        function addBubble(text, type) {
            const div = document.createElement('div');
            div.className = 'bubble ' + type;
            div.textContent = text;
            document.getElementById('chat').appendChild(div);
            div.scrollIntoView({behavior: "smooth"});
        }
        
        function sendMsg() {
            const input = document.getElementById('msg');
            const text = input.value.trim();
            if(!text) return;
            
            const btn = document.querySelector('.send-btn');
            if (btn.disabled) return;
            
            btn.disabled = true;
            btn.style.opacity = '0.5';
            
            addBubble(text, 'user');
            input.value = '';
            document.getElementById('ai-status').textContent = 'Thinking';
            
            fetch('/api/chat', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'text=' + encodeURIComponent(text)
            }).then(r => {
                if(r.status === 429) {
                    addBubble("Sistem sedang sibuk! Harap tunggu...", "ai");
                    btn.disabled = false;
                    btn.style.opacity = '1';
                    document.getElementById('ai-status').textContent = 'Busy';
                    return;
                }
                checkReply();
            }).catch(err => {
                addBubble('Error connection', 'ai');
                document.getElementById('ai-status').textContent = 'Error';
                btn.disabled = false;
                btn.style.opacity = '1';
            });
        }
        
        var replyAttempts = 0;
        function checkReply() {
            replyAttempts++;
            if (replyAttempts > 30) {
                addBubble('Timeout: server tidak merespons.', 'ai');
                document.getElementById('ai-status').textContent = 'Error';
                const btn = document.querySelector('.send-btn');
                btn.disabled = false;
                btn.style.opacity = '1';
                replyAttempts = 0;
                return;
            }
            fetch('/api/chat_reply').then(r => r.text()).then(reply => {
                if(reply === 'PENDING') {
                    setTimeout(checkReply, 1000);
                } else {
                    addBubble(reply, 'ai');
                    document.getElementById('ai-status').textContent = 'Ready';
                    const btn = document.querySelector('.send-btn');
                    btn.disabled = false;
                    btn.style.opacity = '1';
                    replyAttempts = 0;
                }
            }).catch(() => {
                setTimeout(checkReply, 2000);
            });
        }
        
        setInterval(() => {
            fetch('/api/status').then(r => r.text()).then(s => {
                if(s && document.getElementById('ai-status').textContent.indexOf('...') === -1 && document.getElementById('ai-status').textContent !== 'Busy') {
                    document.getElementById('ai-status').textContent = s;
                }
            });
        }, 1500);
        
        function openSettings() {
            fetch('/api/settings').then(r => r.json()).then(data => {
                document.getElementById('ai_name').value = data.ai_name || "BRAFI AI";
                document.getElementById('sys_prompt').value = data.sys_prompt || "";
                document.getElementById('backend_url').value = data.backend_url || "";
                
                document.getElementById('face_ew').value = data.face_ew || 12;
                document.getElementById('val_ew').innerText = data.face_ew || 12;
                document.getElementById('face_eh').value = data.face_eh || 16;
                document.getElementById('val_eh').innerText = data.face_eh || 16;
                document.getElementById('face_esp').value = data.face_esp || 16;
                document.getElementById('val_esp').innerText = data.face_esp || 16;
                document.getElementById('face_mw').value = data.face_mw || 14;
                document.getElementById('val_mw').innerText = data.face_mw || 14;
                
                document.getElementById('settingsModal').style.display = 'flex';
            });
        }
        
        function closeSettings() {
            document.getElementById('settingsModal').style.display = 'none';
        }
        
        function saveSettings() {
            const ai_name = document.getElementById('ai_name').value;
            const sys_prompt = document.getElementById('sys_prompt').value;
            const backend_url = document.getElementById('backend_url').value;
            const api_key = document.getElementById('api_key').value;
            
            const ew = document.getElementById('face_ew').value;
            const eh = document.getElementById('face_eh').value;
            const esp = document.getElementById('face_esp').value;
            const mw = document.getElementById('face_mw').value;
            
            const btn = document.querySelector('.btn-save');
            btn.textContent = "Saving...";
            
            var body = 'ai_name=' + encodeURIComponent(ai_name) + '&sys_prompt=' + encodeURIComponent(sys_prompt) + '&backend_url=' + encodeURIComponent(backend_url);
            if (api_key) body += '&api_key=' + encodeURIComponent(api_key);
            
            fetch('/api/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: body
            }).then(() => {
                // Save Face Params
                fetch('/api/face', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: 'ew=' + ew + '&eh=' + eh + '&esp=' + esp + '&mw=' + mw
                }).then(() => {
                    document.getElementById('ai-title').textContent = ai_name + " DASHBOARD";
                    document.getElementById('api_key').value = '';
                    switchLLM();
                    closeSettings();
                    btn.textContent = "Save";
                });
            });
        }
        
        fetch('/api/settings').then(r => r.json()).then(data => {
            if(data.ai_name) document.getElementById('ai-title').textContent = data.ai_name + " DASHBOARD";
        });
        
        var wsBackend = null;
        function connectWebSocket() {
            try {
                var backendUrl = document.getElementById('backend_url') ? document.getElementById('backend_url').value : '';
                if (!backendUrl) {
                    fetch('/api/settings').then(r => r.json()).then(data => {
                        if (data.backend_url) {
                            var wsUrl = data.backend_url.replace('http://', 'ws://').replace('https://', 'wss://');
                            startWS(wsUrl + '/ws');
                        }
                    });
                } else {
                    var wsUrl = backendUrl.replace('http://', 'ws://').replace('https://', 'wss://');
                    startWS(wsUrl + '/ws');
                }
            } catch(e) {}
        }
        
        function startWS(url) {
            wsBackend = new WebSocket(url);
            wsBackend.onmessage = function(event) {
                var data = JSON.parse(event.data);
                if (data.type === 'update' || data.type === 'status') {
                    if (data.status) document.getElementById('ai-status').textContent = data.status;
                    if (data.latency_ms) document.getElementById('latency').textContent = data.latency_ms + 'ms';
                    
                    var model = data.source === 'faq_cache' || data.source === 'error' ? (data.engine || 'gemini') : (data.source || data.engine);
                    if (data.token && data.token.all_time_total && data.token.all_time_total[model] !== undefined) {
                        // Format numbers nicely, e.g., 3000000 -> 3M, 1500 -> 1.5K
                        var t = data.token.all_time_total[model];
                        var formatted = t > 1000000 ? (t/1000000).toFixed(1) + 'M' : (t > 1000 ? (t/1000).toFixed(1) + 'K' : t);
                        document.getElementById('token-last').textContent = formatted;
                        document.getElementById('token-label').textContent = model.substring(0,6).toUpperCase() + " TKS";
                    } else if (data.token) {
                        document.getElementById('token-last').textContent = data.token.last_total;
                    }
                    
                    if (data.source) {
                        var srcEl = document.getElementById('source');
                        srcEl.textContent = data.source;
                        srcEl.className = 'badge badge-' + (data.source === 'faq_cache' ? 'faq_cache' : data.source || 'none');
                    }
                }
            };
            wsBackend.onclose = function() { setTimeout(connectWebSocket, 5000); };
            wsBackend.onerror = function() { wsBackend.close(); };
        }
        
        setTimeout(connectWebSocket, 2000);
        
        function switchLLM() {
            var sel = document.getElementById('llm_select');
            if (!sel) return;
            var backendUrl = document.getElementById('backend_url').value || '';
            if (!backendUrl) return;
            var formData = new FormData();
            formData.append('llm', sel.value);
            fetch(backendUrl + '/api/set-llm', { method: 'POST', body: formData }).catch(err => {});
        }
    </script>
</body>
</html>
)=====";

void initCaptivePortal() {
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", setup_html);
    });
    
    server.on("/api/scan", HTTP_GET, []() {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) json += ",";
            // Escape quote jika ada nama WiFi aneh
            String ssid = WiFi.SSID(i);
            ssid.replace("\"", "\\\"");
            json += "\"" + ssid + "\"";
        }
        json += "]";
        WiFi.scanDelete(); // Bebaskan memori
        server.send(200, "application/json", json);
    });
    
    server.on("/save", HTTP_POST, []() {
        if(server.hasArg("ssid") && server.hasArg("pass")) {
            String ssid = server.arg("ssid");
            String pass = server.arg("pass");
            preferences.putString("ssid", ssid);
            preferences.putString("pass", pass);
            
            if(server.hasArg("backend")) {
                String backend = server.arg("backend");
                if (!backend.startsWith("http")) backend = "http://" + backend + ":8000";
                preferences.putString("backend_url", backend);
            }
            
            String res = "<html><body style='background:#0f172a;color:white;text-align:center;padding:50px;font-family:sans-serif;'><h2>Tersimpan!</h2><p>ESP32 sedang di-restart untuk menyambung ke WiFi baru...</p></body></html>";
            server.send(200, "text/html", res);
            delay(2000);
            ESP.restart();
        } else {
            server.send(400, "text/html", "Bad Request");
        }
    });
    
    server.onNotFound([]() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    });
    
    server.begin();
    Serial.println("Captive Portal Started!");
}

void initWebDashboard() {
    current_backend_url = preferences.getString("backend_url", DEFAULT_BACKEND_URL);
    
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", dashboard_html);
    });
    
    server.on("/api/status", HTTP_GET, []() {
        server.send(200, "text/plain", current_ai_status);
    });
    
    // -- Fase 6: LOGIKA KOTAK SURAT VIA QUEUE (NON-BLOCKING) --
    server.on("/api/chat", HTTP_POST, []() {
        // Cek apakah queue penuh (ada request yang belum diproses)
        if (uxQueueMessagesWaiting(chatRequestQueue) > 0) {
            server.send(429, "text/plain", "BUSY");
            return;
        }
        
        if (server.hasArg("text")) {
            ChatMessage msg;
            memset(&msg, 0, sizeof(msg));
            strncpy(msg.request, server.arg("text").c_str(), sizeof(msg.request) - 1);
            xQueueSend(chatRequestQueue, &msg, 0);  // Kirim ke Core 1 via Queue
            server.send(200, "text/plain", "OK");  // Langsung balas ke browser tanpa tunggu
        } else {
            server.send(400, "text/plain", "Missing text");
        }
    });
    
    server.on("/api/chat_reply", HTTP_GET, []() {
        ChatMessage replyMsg;
        if (xQueueReceive(chatReplyQueue, &replyMsg, 0) == pdTRUE) {
            server.send(200, "text/plain", replyMsg.reply);
        } else {
            server.send(200, "text/plain", "PENDING");
        }
    });
    
    // -- SETTINGS API --
    server.on("/api/settings", HTTP_GET, []() {
        String sys_prompt = preferences.getString("sys_prompt", "Kamu adalah asisten pintar bernama BRAFI AI berbahasa Indonesia.");
        String ai_name = preferences.getString("ai_name", "BRAFI AI");
        String backend_url = preferences.getString("backend_url", DEFAULT_BACKEND_URL);
        
        // Baca preferences face
        Preferences facePrefs;
        facePrefs.begin("face", true);
        int ew = facePrefs.getInt("ew", 12);
        int eh = facePrefs.getInt("eh", 16);
        int esp = facePrefs.getInt("esp", 16);
        int mw = facePrefs.getInt("mw", 14);
        facePrefs.end();
        
        // Bersihkan quote (") agar tidak merusak JSON
        sys_prompt.replace("\"", "\\\"");
        ai_name.replace("\"", "\\\"");
        backend_url.replace("\"", "\\\"");
        
        String json = "{\"sys_prompt\":\"" + sys_prompt + "\",\"ai_name\":\"" + ai_name + "\",\"backend_url\":\"" + backend_url + "\",\"has_api_key\":" + (preferences.getString("gemini_key_enc", "").length() > 0 ? "true" : "false") + ",\"face_ew\":" + String(ew) + ",\"face_eh\":" + String(eh) + ",\"face_esp\":" + String(esp) + ",\"face_mw\":" + String(mw) + "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/api/settings", HTTP_POST, []() {
        if (server.hasArg("sys_prompt") && server.hasArg("ai_name")) {
            preferences.putString("sys_prompt", server.arg("sys_prompt"));
            preferences.putString("ai_name", server.arg("ai_name"));
            if (server.hasArg("backend_url")) {
                preferences.putString("backend_url", server.arg("backend_url"));
                current_backend_url = server.arg("backend_url"); // Update langsung di RAM
            }
            // Fase 5: Encrypt dan simpan API key jika dikirim
            if (server.hasArg("api_key") && server.arg("api_key").length() > 0) {
                String encrypted = aes_encrypt_to_hex(server.arg("api_key"));
                preferences.putString("gemini_key_enc", encrypted);
                Serial.println("API Key encrypted and saved.");
            }
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Error");
        }
    });
    
    server.on("/api/face", HTTP_POST, []() {
        if (server.hasArg("ew") && server.hasArg("eh") && server.hasArg("esp") && server.hasArg("mw")) {
            int ew = server.arg("ew").toInt();
            int eh = server.arg("eh").toInt();
            int esp = server.arg("esp").toInt();
            int mw = server.arg("mw").toInt();
            
            Preferences facePrefs;
            facePrefs.begin("face", false);
            facePrefs.putInt("ew", ew);
            facePrefs.putInt("eh", eh);
            facePrefs.putInt("esp", esp);
            facePrefs.putInt("mw", mw);
            facePrefs.end();
            
            if (faceEngine) {
                faceEngine->setFaceParameters(ew, eh, esp, mw, 3, -6, 18);
            }
            
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Error");
        }
    });
    
    server.begin();
    Serial.println("Web Dashboard Started!");
}
