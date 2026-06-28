#include "network.h"
#include "config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include "web.h"
#include "display.h"

bool is_ap_mode = false;
const byte DNS_PORT = 53;
DNSServer dnsServer;
Preferences preferences;

void initWiFi() {
    preferences.begin("wifi_creds", false);
    String ssid = preferences.getString("ssid", WIFI_SSID);
    String pass = preferences.getString("pass", WIFI_PASSWORD);
    
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi Failed! Starting Captive Portal AP...");
        is_ap_mode = true;
        
        WiFi.mode(WIFI_AP);
        WiFi.softAP("BRAFI_AI_SETUP");
        
        delay(100);
        
        // Mulai DNS Server untuk mengalihkan semua request ke IP ESP32
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        
        // Mulai Web Server untuk Setup
        initCaptivePortal();
        
        // Tampilkan instruksi di OLED
        clearDisplay();
        Adafruit_SSD1306* disp = getDisplay();
        if (disp) {
            disp->setTextSize(1);
            disp->setCursor(0, 10);
            disp->print("Connect to WiFi:");
            disp->setCursor(0, 30);
            disp->print("BRAFI_AI_SETUP");
            updateDisplay();
        }
    } else {
        Serial.println("\nWiFi Connected!");
        Serial.println(WiFi.localIP());
        is_ap_mode = false;
        initWebDashboard();
        
        clearDisplay();
        Adafruit_SSD1306* disp = getDisplay();
        if (disp) {
            disp->setTextSize(1);
            disp->setCursor(0, 10);
            disp->print("WiFi Connected!");
            disp->setCursor(0, 30);
            disp->print("IP: ");
            disp->print(WiFi.localIP());
            updateDisplay();
            delay(4000); // Tahan layar 4 detik agar IP mudah dibaca
        }
    }
}

void handleWiFiLoop() {
    if (is_ap_mode) {
        dnsServer.processNextRequest();
    }
}
