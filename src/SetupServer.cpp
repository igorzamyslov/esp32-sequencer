#include "SetupServer.h"
#include "ConfigForm.h"
#include <WiFi.h>
#include <ArduinoJson.h>

void SetupServer::begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("esp32-tv-setup");
    Serial.printf("[setup] AP up at %s\n", WiFi.softAPIP().toString().c_str());

    server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* r){ handleRoot(r); });
    server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r){ handleSave(r); });
    server_.on("/pair-start", HTTP_POST, [this](AsyncWebServerRequest* r){ handlePairStart(r); });
    server_.on("/pair-status", HTTP_GET, [this](AsyncWebServerRequest* r){ handlePairStatus(r); });
    server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest* r){ handleReset(r); });
    server_.onNotFound([](AsyncWebServerRequest* r){ r->redirect("http://192.168.4.1/"); });
    server_.begin();

    dns_.start(53, "*", WiFi.softAPIP());

    scanner_.begin();
    scanner_.onHit([this](const BleHit& h){
        if (!pairing_) return;
        if (millis() - pair_started_at_ > 30000) { pairing_ = false; scanner_.stop(); return; }
        if (h.rssi > best_ds_rssi_) {
            best_ds_rssi_ = h.rssi;
            best_ds_mac_ = h.mac;
        }
    });
}

void SetupServer::loop() {
    dns_.processNextRequest();
}

void SetupServer::handleRoot(AsyncWebServerRequest* req) {
    req->send(200, "text/html", ConfigForm::renderHtml(cfg_, true));
}

void SetupServer::handleSave(AsyncWebServerRequest* req) {
    ConfigForm::applySave(req, cfg_);
    req->send(200, "text/plain", "saved — rebooting in 2s");
    delay(2000);
    ESP.restart();
}

void SetupServer::handlePairStart(AsyncWebServerRequest* req) {
    best_ds_mac_ = "";
    best_ds_rssi_ = -127;
    pairing_ = true;
    pair_started_at_ = millis();
    scanner_.start(30000);
    req->send(200, "text/plain", "scanning");
}

void SetupServer::handlePairStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["mac"] = best_ds_mac_;
    doc["rssi"] = best_ds_rssi_;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void SetupServer::handleReset(AsyncWebServerRequest* req) {
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(1000);
    ESP.restart();
}
