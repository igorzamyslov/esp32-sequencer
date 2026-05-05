#pragma once
#include <Arduino.h>

// Bootstrap-only: credentials for the idle WiFi network (where the ESP idles
// and serves the web UI) plus the runtime-managed Samsung pairing token.
// Per-device parameters and *other* WiFi networks live as block params in
// sequences.json.
struct Config {
    String idleSsid;
    String idlePass;
    String tvToken;        // runtime-managed Samsung Tizen pairing token
    bool setupFallback = false; // SoftAP setup mode auto-starts after persistent wifi failure

    static Config load();
    void save() const;
    static void clear();

    static void requestSetupOnNextBoot();
    static bool consumeSetupRequest();

    bool hasAll() const {
        return idleSsid.length() && idlePass.length();
    }
};
