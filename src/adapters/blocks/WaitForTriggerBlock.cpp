#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr BlockSchema SCH = {
    "wait-for-trigger",
    "Wait for trigger (no-op in v1)",
    "Flow",
    nullptr, 0, nullptr, 0,
};

class WaitForTriggerBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst, const std::map<std::string, std::vector<Node>>&,
                  RunCtx&, Interpreter&) override {
        Serial.println("[seq] wait-for-trigger: noop in v1");
        return RunResult::ok();
    }
};

WaitForTriggerBlock& instance(){ static WaitForTriggerBlock i; return i; }

}

namespace seqb { void registerWaitForTriggerBlock() { Registry::instance().registerBlock(&instance()); } }
