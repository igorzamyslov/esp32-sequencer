#include "BleMacTrigger.h"
#include "core/Registry.h"
#include "BleScanner.h"
#include <Arduino.h>
#include <map>

using namespace seqb;

namespace {

BleScanner* g_scanner = nullptr;

constexpr FieldDef FIELDS[] = {
    {"mac",         FieldType::MacAddress, "Device MAC", nullptr, nullptr, true},
    {"cooldown_ms", FieldType::Int,        "Cooldown (ms)", "60000", nullptr, false},
};
constexpr TriggerSchema SCH = {"ble-mac", "BLE MAC detected", FIELDS, 2, true};

struct Active {
    std::string mac;        // lowercase
    std::string sequenceId;
    Trigger::FireCallback cb;
    uint32_t cooldownMs;
    uint32_t lastFireMs = 0;
};
std::map<std::string, Active> g_active;

std::string toLower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    return out;
}

}

namespace seqb {

void BleMacTrigger::setScanner(BleScanner* s) { g_scanner = s; }
BleMacTrigger& BleMacTrigger::instance() { static BleMacTrigger i; return i; }
const TriggerSchema& BleMacTrigger::schema() const { return SCH; }

void BleMacTrigger::bind(const std::string& id, JsonVariantConst params,
                         const std::string& seq, FireCallback cb) {
    const char* mac = params["mac"].as<const char*>();
    if (!mac) return;
    Active a;
    a.mac = toLower(mac);
    a.sequenceId = seq;
    a.cb = cb;
    a.cooldownMs = params["cooldown_ms"] | 60000U;
    g_active[id] = a;
}

void BleMacTrigger::unbind(const std::string& id) { g_active.erase(id); }

void BleMacTrigger::onHit(const std::string& macLower, int /*rssi*/) {
    uint32_t now = millis();
    for (auto& kv : g_active) {
        auto& a = kv.second;
        if (a.mac != macLower) continue;
        if (now - a.lastFireMs < a.cooldownMs) continue;
        a.lastFireMs = now;
        a.cb(a.sequenceId);
    }
}

void registerBleMacTrigger() { Registry::instance().registerTrigger(&BleMacTrigger::instance()); }

}
