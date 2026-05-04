#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"ms", FieldType::Int, "Milliseconds", "1000", nullptr, true},
};
constexpr BlockSchema SCH = {"wait", "Wait", "Flow", FIELDS, 1, nullptr, 0};

class WaitBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&, Interpreter&) override {
        uint32_t ms = params["ms"] | 0;
        if (ms) delay(ms);
        return RunResult::ok();
    }
};

WaitBlock& instance(){ static WaitBlock i; return i; }
struct _Reg { _Reg(){ Registry::instance().registerBlock(&instance()); } } _reg;

}
