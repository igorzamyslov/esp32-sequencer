#pragma once
#include <Arduino.h>
#include <functional>

struct BleHit {
    String mac;  // lowercase "aa:bb:cc:dd:ee:ff"
    int rssi;
    String name;
};

class BleScanner {
public:
    using HitCallback = std::function<void(const BleHit&)>;

    void begin();
    void onHit(HitCallback cb) { cb_ = cb; }
    void start(uint32_t duration_ms = 0);  // 0 = continuous
    void stop();

    // Called by the NimBLE callback shim — public for that reason, not for users.
    void onHitInternal(const BleHit& h);

    // Match predicate: any advertiser whose name contains "Wireless Controller"
    // OR whose manufacturer data starts with Sony's company ID (0x4C 0x00, little-endian).
    static bool looksLikeDualSense(const String& name, const uint8_t* mfg, size_t mfg_len);

private:
    HitCallback cb_;
    bool started_ = false;
};
