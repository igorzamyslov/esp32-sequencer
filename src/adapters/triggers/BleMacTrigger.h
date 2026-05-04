#pragma once
#include "core/Trigger.h"
class BleScanner;
namespace seqb {
class BleMacTrigger : public Trigger {
public:
    static void setScanner(BleScanner* s);
    static BleMacTrigger& instance();
    const TriggerSchema& schema() const override;
    void bind(const std::string& bindingId, JsonVariantConst params,
              const std::string& sequenceId, FireCallback onFire) override;
    void unbind(const std::string& bindingId) override;
    // called by the BLE scanner hit callback in main
    void onHit(const std::string& macLower, int rssi);
};
}
