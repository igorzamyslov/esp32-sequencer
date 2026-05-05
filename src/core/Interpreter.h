#pragma once
#include "Block.h"
#include "Sequence.h"
namespace seqb {
class Registry;
class Interpreter {
public:
    explicit Interpreter(Registry& r) : reg_(r) {}
    RunResult runSequence(const Sequence& s, RunCtx& ctx);
    RunResult runNode(const Node& n, RunCtx& ctx);
    RunResult runSlot(const std::vector<Node>& nodes, RunCtx& ctx);

private:
    Registry& reg_;
};
}  // namespace seqb
