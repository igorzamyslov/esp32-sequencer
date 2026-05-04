#pragma once
#include <Arduino.h>

// Bootstrap-only config: WiFi credentials needed to bring the device online and
// the runtime-managed Samsung pairing token. Per-device parameters (PC MAC,
// TV MAC/IP, DualSense MAC) live as block/trigger params in sequences.json.
struct Config {
    String tplinkSsid;
    String tplinkPass;
    String tplinkStaticIp; // optional; empty = DHCP. Required when PC's ICS isn't up yet.
    String tplinkGateway;  // optional; only used when tplinkStaticIp is set. Mask is /24.
    String fritzboxSsid;
    String fritzboxPass;
    String tvToken;        // runtime-managed Samsung Tizen pairing token
    bool setupFallback = false; // if true, SoftAP setup mode auto-starts after persistent wifi failure

    static Config load();
    void save() const;
    static void clear();

    // One-shot flag that survives reboot and forces the next boot into setup mode
    // even when the stored config is complete.
    static void requestSetupOnNextBoot();
    static bool consumeSetupRequest();

    // Has every value needed to enter runtime mode.
    bool hasAll() const {
        return tplinkSsid.length() && tplinkPass.length()
            && fritzboxSsid.length() && fritzboxPass.length();
    }
};
