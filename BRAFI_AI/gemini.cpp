#include "gemini.h"
#include "config.h"
#include "web.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

extern Preferences preferences;
String latest_audio_url = "";
static const int ESP_REPLY_TEXT_LIMIT = 700;

static bool parseHttpUrl(const String& url, String& host, uint16_t& port, String& path) {
    if (!url.startsWith("http://")) return false;

    int hostStart = 7;
    int pathStart = url.indexOf('/', hostStart);
    String hostPort = pathStart >= 0 ? url.substring(hostStart, pathStart) : url.substring(hostStart);
    path = pathStart >= 0 ? url.substring(pathStart) : "/";

    int colonPos = hostPort.lastIndexOf(':');
    if (colonPos >= 0) {
        host = hostPort.substring(0, colonPos);
        port = (uint16_t)hostPort.substring(colonPos + 1).toInt();
        if (port == 0) return false;
    } else {
        host = hostPort;
        port = 80;
    }

    return host.length() > 0;
}

static String buildBackendAudioUrl(const String& audioPath) {
    if (audioPath.startsWith("http://") || audioPath.startsWith("https://")) {
        return audioPath;
    }
    if (audioPath.startsWith("/")) {
        return current_backend_url + audioPath;
    }
    return current_backend_url + "/" + audioPath;
}

// Helper untuk URL Encode
String urlEncode(const String& str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < str.length(); i++){
        c = str.charAt(i);
        if (c == ' '){
            encodedString += '+';
        } else if (isalnum(c)){
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) code0 = c - 10 + 'A';
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

// Mengirimkan rekaman audio ke Backend Python lokal
String askGeminiAudio(const uint8_t* audioData, size_t audioLen) {
    latest_audio_url = "";
    if (current_backend_url == "") {
        return "Err: Backend URL belum diatur!";
    }
    
    Serial.println("Gemini: Mengirim Audio ke Backend " + current_backend_url);
    
    String host;
    String basePath;
    uint16_t port = 80;
    if (!parseHttpUrl(current_backend_url, host, port, basePath)) {
        return "Err: Backend URL harus memakai format http://host:port";
    }
    
    // Setup Multipart form
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String contentType = "multipart/form-data; boundary=" + boundary;
    
    String sys_prompt = preferences.getString("sys_prompt", "Kamu adalah asisten pintar bernama BRAFI AI berbahasa Indonesia.");
    
    // Body header
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"sys_prompt\"\r\n\r\n";
    bodyStart += sys_prompt + "\r\n";
    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"max_reply_chars\"\r\n\r\n";
    bodyStart += String(ESP_REPLY_TEXT_LIMIT) + "\r\n";
    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"audio\"; filename=\"mic.wav\"\r\n";
    bodyStart += "Content-Type: audio/wav\r\n\r\n";
    
    String bodyEnd = "\r\n--" + boundary + "--\r\n";
    
    size_t contentLength = bodyStart.length() + audioLen + bodyEnd.length();
    
    WiFiClient client;
    if (client.connect(host.c_str(), port)) {
        client.println("POST /api/chat HTTP/1.1");
        client.println("Host: " + host + ":" + String(port));
        client.println("Content-Type: " + contentType);
        client.println("Content-Length: " + String(contentLength));
        client.println("Connection: close");
        client.println();
        
        client.print(bodyStart);
        
        // Kirim audio
        size_t remaining = audioLen;
        size_t sent = 0;
        const size_t chunkSize = 2048; // Mengirim per chunk
        
        while(remaining > 0 && client.connected()) {
            size_t toSend = remaining < chunkSize ? remaining : chunkSize;
            size_t written = client.write(audioData + sent, toSend);
            if (written == 0) {
                Serial.println("Upload audio terhenti: socket tidak menerima data.");
                break;
            }
            sent += written;
            remaining -= written;
            delay(1);
            yield();
        }

        if (remaining > 0) {
            client.stop();
            Serial.println("Upload audio gagal: data tidak terkirim penuh.");
            return "Maaf, upload audio ke server lokal gagal.";
        }
        
        // Kirim body end
        client.print(bodyEnd);
        
        // Baca respons
        String responseBody = "";
        bool isBody = false;
        unsigned long timeout = millis();
        while ((client.connected() || client.available()) && millis() - timeout < 25000) {
            if (client.available()) {
                String line = client.readStringUntil('\n');
                if (line == "\r") {
                    isBody = true;
                } else if (isBody) {
                    responseBody += line;
                }
                timeout = millis();
            } else {
                delay(1);
                yield();
            }
        }
        client.stop();
        
        Serial.println("Backend Respon: " + responseBody);
        
        // Parsing JSON
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, responseBody);
        if (!error) {
            if (doc["status"] == "success") {
                latest_audio_url = buildBackendAudioUrl(doc["audio_url"].as<String>());
                return doc["reply_text"].as<String>();
            }
            Serial.println("Backend status bukan success: " + doc["status"].as<String>());
        } else {
            Serial.println("JSON parse audio gagal: " + String(error.c_str()));
        }
    } else {
        Serial.println("Gagal terhubung ke soket backend.");
    }
    
    return "Maaf, saya tidak bisa terhubung ke server lokal.";
}

// Mengirimkan teks ke Backend Python (dari Web Chat)
String askGeminiText(const String& textPrompt) {
    latest_audio_url = "";
    if (current_backend_url == "") {
        return "Err: Backend URL belum diatur!";
    }
    
    Serial.println("Gemini: Mengirim Teks ke Backend " + current_backend_url);
    
    String url = current_backend_url + "/api/chat";
    WiFiClient client;
    HTTPClient http;
    
    http.begin(client, url);
    http.setTimeout(20000);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String sys_prompt = preferences.getString("sys_prompt", "Kamu adalah asisten pintar bernama BRAFI AI berbahasa Indonesia.");
    String postData = "text=" + urlEncode(textPrompt)
        + "&sys_prompt=" + urlEncode(sys_prompt)
        + "&max_reply_chars=" + String(ESP_REPLY_TEXT_LIMIT);
    
    int httpCode = http.POST(postData);
    
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            if (doc["status"] == "success") {
                latest_audio_url = buildBackendAudioUrl(doc["audio_url"].as<String>());
                String reply = doc["reply_text"].as<String>();
                http.end();
                return reply;
            }
            Serial.println("Backend status bukan success: " + doc["status"].as<String>());
        } else {
            Serial.println("JSON parse teks gagal: " + String(error.c_str()));
        }
    } else {
        Serial.println("HTTP teks gagal, code: " + String(httpCode));
    }
    
    http.end();
    return "Koneksi ke backend lokal terputus.";
}
