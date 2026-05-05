#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include "NetworkManager.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"ssid",       FieldType::String, "SSID",                       nullptr, nullptr, true},
    {"password",   FieldType::String, "Password",                   nullptr, nullptr, false},
    {"static_ip",  FieldType::String, "Static IP (optional)",       nullptr, nullptr, false},
    {"gateway",    FieldType::String, "Gateway (with static IP)",   nullptr, nullptr, false},
    {"timeout_ms", FieldType::Int,    "Connect timeout (ms)",       "15000", nullptr, false},
};
constexpr BlockSchema SCH = {"wifi-hop", "WiFi hop", "Network", FIELDS, 5, nullptr, 0};

class WifiHopBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx& ctx, Interpreter&) override {
        if (!ctx.net) return RunResult::failed("wifi-hop: no network manager");
        const char* ssid = params["ssid"].as<const char*>();
        if (!ssid || !ssid[0]) return RunResult::failed("wifi-hop: missing ssid");
        const char* pass = params["password"].as<const char*>();
        const char* sIp  = params["static_ip"].as<const char*>();
        const char* gw   = params["gateway"].as<const char*>();
        uint32_t toMs = params["timeout_ms"] | 15000;
        if (ctx.net->currentSsid() == String(ssid) && ctx.net->isConnected()) return RunResult::ok();
        if (!ctx.net->connect(String(ssid), String(pass ? pass : ""),
                              String(sIp ? sIp : ""), String(gw ? gw : ""), toMs)) {
            return RunResult::failed("wifi-hop: connect failed");
        }
        return RunResult::ok();
    }
};

WifiHopBlock& instance(){ static WifiHopBlock i; return i; }
struct _Reg { _Reg(){ Registry::instance().registerBlock(&instance()); } } _reg;

}
