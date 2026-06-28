#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

extern bool is_ap_mode;

/**
 * @brief Memulai koneksi WiFi. Jika gagal, akan masuk ke Mode AP (Captive Portal).
 */
void initWiFi();

/**
 * @brief Menjalankan loop untuk DNSServer saat mode AP.
 */
void handleWiFiLoop();

#endif // NETWORK_H
