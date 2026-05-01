#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "BleScanner.h"

class SetupServer {
public:
    SetupServer(Config& cfg, BleScanner& scanner) : cfg_(cfg), scanner_(scanner), server_(80) {}
    void begin();
private:
    Config& cfg_;
    BleScanner& scanner_;
    AsyncWebServer server_;

    // Pair-gamepad state
    String best_ds_mac_;
    int best_ds_rssi_ = -127;
    bool pairing_ = false;
    uint32_t pair_started_at_ = 0;

    void handleRoot(AsyncWebServerRequest* req);
    void handleSave(AsyncWebServerRequest* req);
    void handlePairStart(AsyncWebServerRequest* req);
    void handlePairStatus(AsyncWebServerRequest* req);
    void handleReset(AsyncWebServerRequest* req);
};
