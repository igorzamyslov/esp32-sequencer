#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"predicate", FieldType::PredicateRef, "Condition", nullptr, nullptr, true},
};
constexpr const char* SLOTS[] = {"then", "else"};
constexpr BlockSchema SCH = {"if", "If", "Flow", FIELDS, 1, SLOTS, 2};

class IfBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        return RunResult::ok();
    }
};

IfBlock& instance() {
    static IfBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
