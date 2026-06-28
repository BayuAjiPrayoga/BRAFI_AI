#include "mqtt.h"
#include "config.h"
#include "tts.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

extern Preferences preferences;

// MQTT Broker — sama dengan yang dipakai backend Python
static const char* MQTT_BROKER = "broker.hivemq.com";
static const int MQTT_PORT = 1883;
static const char* MQTT_CLIENT_ID = "brafi_esp32";

static WiFiClient mqttWifiClient;
static PubSubClient mqttClient(mqttWifiClient);

static unsigned long lastReconnectAttempt = 0;

// Callback saat menerima pesan MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.print("MQTT Received [");
    Serial.print(topicStr);
    Serial.print("]: ");
    Serial.println(message);
    
    // Command: Speak — TTS-kan teks yang diterima
    if (topicStr == "brafi/command/speak") {
        if (message.length() > 0) {
            Serial.println("MQTT Command: Speaking...");
            playTTS(message);
        }
    }
    // Command: Reset WiFi — hapus credentials dan restart
    else if (topicStr == "brafi/command/reset_wifi") {
        Serial.println("MQTT Command: Resetting WiFi...");
        Preferences prefs;
        prefs.begin("wifi_creds", false);
        prefs.clear();
        prefs.end();
        delay(500);
        ESP.restart();
    }
    // Command: Reboot
    else if (topicStr == "brafi/command/reboot") {
        Serial.println("MQTT Command: Rebooting...");
        delay(500);
        ESP.restart();
    }
}

// Reconnect ke broker MQTT
static bool mqttReconnect() {
    Serial.print("MQTT: Connecting to ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");
    
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
        Serial.println(" connected!");
        
        // Subscribe ke command topics
        mqttClient.subscribe("brafi/command/speak");
        mqttClient.subscribe("brafi/command/reset_wifi");
        mqttClient.subscribe("brafi/command/reboot");
        
        // Publish status online
        mqttClient.publish("brafi/status", "Online", false);
        
        return true;
    } else {
        Serial.print(" failed, rc=");
        Serial.println(mqttClient.state());
        return false;
    }
}

void initMQTT() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);  // Buffer cukup untuk payload command
    
    // Coba connect sekali saat init
    mqttReconnect();
}

void mqttLoop() {
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        // Auto-reconnect setiap 5 detik
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            mqttReconnect();
        }
    } else {
        mqttClient.loop();
    }
}

void mqttPublish(const char* topic, const char* payload) {
    if (mqttClient.connected()) {
        String fullTopic = "brafi/" + String(topic);
        mqttClient.publish(fullTopic.c_str(), payload, false);
    }
}

void mqttPublishHeap() {
    if (mqttClient.connected()) {
        String heap = String(ESP.getFreeHeap());
        mqttClient.publish("brafi/heap", heap.c_str(), false);
    }
}
