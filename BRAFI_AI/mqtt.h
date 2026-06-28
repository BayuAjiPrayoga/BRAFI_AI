#ifndef MQTT_H
#define MQTT_H
#include <Arduino.h>

void initMQTT();
void mqttLoop();
void mqttPublish(const char* topic, const char* payload);
void mqttPublishHeap();

#endif
