#pragma once
#include <Arduino.h>

enum class WifiTarget { None, TpLink, Fritzbox };

class NetworkManager {
public:
    void configure(const String& tpSsid, const String& tpPass,
                   const String& fbSsid, const String& fbPass);

    // Blocks up to timeout_ms. Returns true on success.
    bool connect(WifiTarget target, uint32_t timeout_ms = 15000);
    void disconnect();
    WifiTarget current() const { return current_; }

    // Convenience wrappers:
    bool connectTplink(uint32_t t = 15000)    { return connect(WifiTarget::TpLink, t); }
    bool connectFritzbox(uint32_t t = 15000)  { return connect(WifiTarget::Fritzbox, t); }

    // Disconnect + connect to a different target. Returns true on success.
    bool hopTo(WifiTarget target, uint32_t timeout_ms = 15000);

    String localIp() const;

private:
    String tpSsid_, tpPass_, fbSsid_, fbPass_;
    WifiTarget current_ = WifiTarget::None;
};
