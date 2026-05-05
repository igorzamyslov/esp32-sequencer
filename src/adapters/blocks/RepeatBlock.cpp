#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"count",       FieldType::Int, "Iterations",                "1", nullptr, true},
    {"interval_ms", FieldType::Int, "Wait between iterations (ms)", "0", nullptr, false},
};
constexpr const char* SLOTS[] = {"body"};
constexpr BlockSchema SCH = {"repeat", "Repeat", "Flow", FIELDS, 2, SLOTS, 1};

class RepeatBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    // Never called: interpreter handles "repeat" intrinsically.
    RunResult run(JsonVariantConst, const std::map<std::string, std::vector<Node>>&,
                  RunCtx&, Interpreter&) override { return RunResult::ok(); }
};

RepeatBlock& instance(){ static RepeatBlock i; return i; }
struct _Reg { _Reg(){ Registry::instance().registerBlock(&instance()); } } _reg;

}
