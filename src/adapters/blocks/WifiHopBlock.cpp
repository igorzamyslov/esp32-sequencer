#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include "NetworkManager.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"target", FieldType::Enum, "Network", "fritzbox", "tplink,fritzbox", true},
};
constexpr BlockSchema SCH = {"wifi-hop", "WiFi hop", "Network", FIELDS, 1, nullptr, 0};

class WifiHopBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx& ctx, Interpreter&) override {
        if (!ctx.net) return RunResult::failed("wifi-hop: no network manager");
        const char* t = params["target"].as<const char*>();
        WifiTarget target = (t && std::string(t) == "tplink") ? WifiTarget::TpLink : WifiTarget::Fritzbox;
        if (ctx.net->current() == target) return RunResult::ok();
        if (!ctx.net->hopTo(target)) return RunResult::failed("wifi-hop: connect failed");
        return RunResult::ok();
    }
};

WifiHopBlock& instance(){ static WifiHopBlock i; return i; }

}

namespace seqb { void registerWifiHopBlock() { Registry::instance().registerBlock(&instance()); } }
