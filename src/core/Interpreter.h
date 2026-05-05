#pragma once
#include "Block.h"
#include "Sequence.h"
#include <ArduinoJson.h>

namespace seqb {
class Registry;
class Interpreter {
public:
    explicit Interpreter(Registry& r) : reg_(r) {}

    // Seeds the scope from sequence defaults overlaid with `args`,
    // then runs the body. `args` may be a null variant -> defaults only.
    RunResult
    runSequence(const Sequence& s, RunCtx& ctx, JsonVariantConst args = JsonVariantConst());

    RunResult runNode(const Node& n, RunCtx& ctx);
    RunResult runSlot(const std::vector<Node>& nodes, RunCtx& ctx);

private:
    static constexpr int MAX_CALL_DEPTH = 8;
    Registry& reg_;
};
}  // namespace seqb
