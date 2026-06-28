#ifndef WEB_H
#define WEB_H

#include <WebServer.h>

extern WebServer server;

void initCaptivePortal();
void initWebDashboard();
extern String current_backend_url;

// Fase 5: AES decrypt untuk API key (dipakai oleh gemini.cpp)
String aes_decrypt_from_hex(const String& hexStr);

#endif // WEB_H
