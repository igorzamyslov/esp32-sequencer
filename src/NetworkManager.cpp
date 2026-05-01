#include "NetworkManager.h"
#include <WiFi.h>

void NetworkManager::configure(const String& tpSsid, const String& tpPass,
                               const String& fbSsid, const String& fbPass) {
    tpSsid_ = tpSsid; tpPass_ = tpPass;
    fbSsid_ = fbSsid; fbPass_ = fbPass;
}

bool NetworkManager::connect(WifiTarget target, uint32_t timeout_ms) {
    if (current_ == target && WiFi.isConnected()) return true;
    if (WiFi.isConnected()) WiFi.disconnect(true, true);

    const char* ssid = nullptr;
    const char* pass = nullptr;
    if (target == WifiTarget::TpLink) {
        ssid = tpSsid_.c_str(); pass = tpPass_.c_str();
    } else if (target == WifiTarget::Fritzbox) {
        ssid = fbSsid_.c_str(); pass = fbPass_.c_str();
    } else {
        return false;
    }

    Serial.printf("[net] connecting to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (WiFi.isConnected()) {
            current_ = target;
            Serial.printf("[net] connected ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(100);
    }
    Serial.printf("[net] timeout connecting to %s\n", ssid);
    WiFi.disconnect(true, true);
    current_ = WifiTarget::None;
    return false;
}

void NetworkManager::disconnect() {
    if (WiFi.isConnected()) WiFi.disconnect(true, true);
    current_ = WifiTarget::None;
}

bool NetworkManager::hopTo(WifiTarget target, uint32_t timeout_ms) {
    disconnect();
    delay(200);
    return connect(target, timeout_ms);
}

String NetworkManager::localIp() const {
    return WiFi.isConnected() ? WiFi.localIP().toString() : String("");
}
