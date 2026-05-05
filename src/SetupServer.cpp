#include "SetupServer.h"
#include "ConfigForm.h"
#include <WiFi.h>

void SetupServer::begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("esp32-sequencer-setup");
    Serial.printf("[setup] AP up at %s\n", WiFi.softAPIP().toString().c_str());

    server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRoot(r); });
    server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r) { handleSave(r); });
    server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest* r) { handleReset(r); });
    server_.onNotFound([](AsyncWebServerRequest* r) { r->redirect("http://192.168.4.1/"); });
    server_.begin();

    dns_.start(53, "*", WiFi.softAPIP());
}

void SetupServer::loop() {
    dns_.processNextRequest();
}

void SetupServer::handleRoot(AsyncWebServerRequest* req) {
    req->send(200, "text/html", ConfigForm::renderHtml(cfg_));
}

void SetupServer::handleSave(AsyncWebServerRequest* req) {
    ConfigForm::applySave(req, cfg_);
    req->send(200, "text/plain", "saved — rebooting in 2s");
    delay(2000);
    ESP.restart();
}

void SetupServer::handleReset(AsyncWebServerRequest* req) {
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(1000);
    ESP.restart();
}
