#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "Config.h"

class BleScanner;

class SetupServer {
public:
    SetupServer(Config& cfg, BleScanner& /*scanner_unused*/) : cfg_(cfg), server_(80) {}
    void begin();
    void loop();
private:
    Config& cfg_;
    AsyncWebServer server_;
    DNSServer dns_;

    void handleRoot(AsyncWebServerRequest* req);
    void handleSave(AsyncWebServerRequest* req);
    void handleReset(AsyncWebServerRequest* req);
};
