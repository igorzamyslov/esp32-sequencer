#include "NetworkManager.h"
#include <WiFi.h>

bool NetworkManager::connect(const String& ssid, const String& pass,
                             const String& staticIp, const String& gateway,
                             uint32_t timeout_ms) {
    if (current_ == ssid && WiFi.isConnected()) return true;
    if (WiFi.isConnected()) WiFi.disconnect(true, true);
    delay(200);

    Serial.printf("[net] connecting to %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);

    if (staticIp.length() && gateway.length()) {
        IPAddress ip, gw, mask(255, 255, 255, 0);
        if (ip.fromString(staticIp) && gw.fromString(gateway)) {
            // gateway also doubles as DNS (ICS host runs DNS forwarder)
            WiFi.config(ip, gw, mask, gw);
            Serial.printf("[net] using static ip=%s gw=%s\n", staticIp.c_str(), gateway.c_str());
        } else {
            Serial.println("[net] invalid static ip/gateway, falling back to DHCP");
            WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
        }
    } else {
        // clear any prior static config (e.g. when hopping back to a DHCP network)
        WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
    }

    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (WiFi.isConnected()) {
            // Disable modem-sleep so HTTP responses don't wait for the next DTIM.
            // Costs a bit of power but the device is mains-powered.
            WiFi.setSleep(WIFI_PS_NONE);
            current_ = ssid;
            Serial.printf("[net] connected ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(100);
    }
    Serial.printf("[net] timeout connecting to %s\n", ssid.c_str());
    WiFi.disconnect(true, true);
    current_ = "";
    return false;
}

void NetworkManager::disconnect() {
    if (WiFi.isConnected()) WiFi.disconnect(true, true);
    current_ = "";
}

String NetworkManager::localIp() const {
    return WiFi.isConnected() ? WiFi.localIP().toString() : String("");
}

bool NetworkManager::isConnected() const {
    return WiFi.isConnected();
}
