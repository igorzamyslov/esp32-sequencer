#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"text", FieldType::String, "Caption", "—", nullptr, false},
};
constexpr BlockSchema SCH = {"divider", "Divider", "Flow", FIELDS, 1, nullptr, 0};

class DividerBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    // Visual-only: interpreter recognises the type and skips it.
    RunResult run(JsonVariantConst,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        return RunResult::ok();
    }
};

DividerBlock& instance() {
    static DividerBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
