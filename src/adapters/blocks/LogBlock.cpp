#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"text", FieldType::String, "Message", "", nullptr, true},
    {"level", FieldType::Enum, "Level", "info", "info,warn,error", false},
};
constexpr BlockSchema SCH = {"log", "Log message", "Flow", FIELDS, 2, nullptr, 0};

class LogBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        const char* text = params["text"].as<const char*>();
        const char* level = params["level"] | "info";
        if (!text) text = "";
        Serial.printf("[seq:%s] %s\n", level, text);
        return RunResult::ok();
    }
};

LogBlock& instance() {
    static LogBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
