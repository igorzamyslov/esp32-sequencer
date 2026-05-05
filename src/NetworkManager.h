#pragma once
#include <Arduino.h>

class NetworkManager {
public:
    // Connect to an arbitrary network. staticIp/gateway empty → DHCP.
    bool connect(const String& ssid, const String& pass,
                 const String& staticIp, const String& gateway,
                 uint32_t timeout_ms = 15000);

    // Connect to the idle network (DHCP). Convenience for boot path.
    bool connectIdle(const String& ssid, const String& pass, uint32_t timeout_ms = 15000) {
        return connect(ssid, pass, "", "", timeout_ms);
    }

    void disconnect();

    // Currently-connected SSID, "" if not connected.
    String currentSsid() const { return current_; }
    String localIp() const;
    bool   isConnected() const;

private:
    String current_;  // SSID or "" if disconnected
};
