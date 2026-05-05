#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include "Wol.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"mac", FieldType::MacAddress, "Target MAC", nullptr, nullptr, true},
};
constexpr BlockSchema SCH = {"wol", "Wake-on-LAN", "Network", FIELDS, 1, nullptr, 0};

class WolBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        const char* mac = params["mac"].as<const char*>();
        if (!mac || !mac[0]) return RunResult::failed("wol: missing mac");
        if (!Wol::sendBroadcast(mac)) return RunResult::failed("wol: send failed");
        return RunResult::ok();
    }
};

WolBlock& instance() {
    static WolBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
