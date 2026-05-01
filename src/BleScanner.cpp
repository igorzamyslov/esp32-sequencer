#include "BleScanner.h"
#include <NimBLEDevice.h>

namespace {
    BleScanner* g_self = nullptr;

    class ScanCallbacks : public NimBLEScanCallbacks {
        void onResult(const NimBLEAdvertisedDevice* dev) override {
            if (!g_self) return;
            String name = String(dev->getName().c_str());
            const uint8_t* mfg = nullptr;
            size_t mfg_len = 0;
            if (dev->haveManufacturerData()) {
                auto data = dev->getManufacturerData();
                mfg = (const uint8_t*)data.data();
                mfg_len = data.size();
            }
            if (!BleScanner::looksLikeDualSense(name, mfg, mfg_len)) return;

            BleHit hit;
            hit.mac = String(dev->getAddress().toString().c_str());
            hit.mac.toLowerCase();
            hit.rssi = dev->getRSSI();
            hit.name = name;
            g_self->onHitInternal(hit);
        }
    };

    static ScanCallbacks s_cb;
}

void BleScanner::begin() {
    g_self = this;
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    auto* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_cb, false);
    scan->setActiveScan(false);   // passive — don't probe
    scan->setInterval(100);       // ms
    scan->setWindow(50);          // ms
}

void BleScanner::start(uint32_t duration_ms) {
    if (started_) return;
    auto* scan = NimBLEDevice::getScan();
    scan->start(duration_ms / 1000, false, true); // duration in seconds; 0=continuous
    started_ = true;
}

void BleScanner::stop() {
    if (!started_) return;
    NimBLEDevice::getScan()->stop();
    started_ = false;
}

void BleScanner::onHitInternal(const BleHit& h) { if (cb_) cb_(h); }

bool BleScanner::looksLikeDualSense(const String& name, const uint8_t* mfg, size_t mfg_len) {
    if (name.indexOf("Wireless Controller") >= 0) return true;
    if (mfg && mfg_len >= 2) {
        // Sony Corp company ID 0x054C, little-endian on the wire = 0x4C 0x05
        if (mfg[0] == 0x4C && mfg[1] == 0x05) return true;
    }
    return false;
}
