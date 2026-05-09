#pragma once
#include "core/Trigger.h"
class BleScanner;
namespace seqb {
class BleMacTrigger : public Trigger {
public:
    static void setScanner(BleScanner* s);
    static BleMacTrigger& instance();
    const TriggerSchema& schema() const override;
    void bind(const std::string& bindingId,
              JsonVariantConst params,
              const std::string& sequenceId,
              JsonVariantConst defaultArgs,
              SequenceLookup lookup,
              FireCallback onFire) override;
    void unbind(const std::string& bindingId) override;
    // called by the BLE scanner hit callback in main
    void onHit(const std::string& macLower, int rssi);

    // Number of currently-bound ble-mac triggers. main.cpp uses this to skip
    // starting the BLE scanner entirely when no binding needs it (saves
    // 2.4 GHz airtime on the single-radio C3, dramatically faster HTTP).
    static int activeCount();
};
}  // namespace seqb
