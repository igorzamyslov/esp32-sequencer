#include "NetworkManager.h"
#include <WiFi.h>

void NetworkManager::configure(const String& tpSsid, const String& tpPass,
                               const String& fbSsid, const String& fbPass) {
    tpSsid_ = tpSsid; tpPass_ = tpPass;
    fbSsid_ = fbSsid; fbPass_ = fbPass;
}

void NetworkManager::configureTplinkStatic(const String& ip, const String& gateway) {
    tpStaticIp_ = ip;
    tpGateway_  = gateway;
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

    if (target == WifiTarget::TpLink && tpStaticIp_.length() && tpGateway_.length()) {
        IPAddress ip, gw, mask(255, 255, 255, 0);
        if (ip.fromString(tpStaticIp_) && gw.fromString(tpGateway_)) {
            // gateway also doubles as the DNS server (ICS host runs DNS forwarder)
            WiFi.config(ip, gw, mask, gw);
            Serial.printf("[net] using static ip=%s gw=%s\n",
                          tpStaticIp_.c_str(), tpGateway_.c_str());
        } else {
            Serial.println("[net] invalid static ip/gateway, falling back to DHCP");
            WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
        }
    } else {
        // ensure any prior static config is cleared (e.g. when hopping back from TpLink to Fritzbox)
        WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
    }

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

bool NetworkManager::isConnected() const {
    return WiFi.isConnected();
}
