#include "Interpreter.h"
#include "Registry.h"
#include "Predicate.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace seqb {

namespace {
void interpDelay(uint32_t ms) {
#ifdef ARDUINO
    if (ms) delay(ms);
#else
    (void)ms;
#endif
}
}  // namespace

RunResult Interpreter::runSequence(const Sequence& s, RunCtx& ctx) {
    return runSlot(s.nodes, ctx);
}

RunResult Interpreter::runSlot(const std::vector<Node>& nodes, RunCtx& ctx) {
    for (auto& n : nodes) {
        auto r = runNode(n, ctx);
        if (r.status == RunStatus::Failed) return r;
    }
    return RunResult::ok();
}

RunResult Interpreter::runNode(const Node& n, RunCtx& ctx) {
    // Honor the editor-side _disabled flag — skip silently.
    {
        JsonVariantConst dis = n.params["_disabled"];
        if (!dis.isNull() && dis.as<bool>()) return RunResult::ok();
    }
    // Editor-only marker: divider block has no effect at runtime.
    if (n.type == "divider") return RunResult::ok();
    if (n.type == "if") {
        auto pp = n.params["predicate"];
        const char* ptype = pp["type"].as<const char*>();
        if (!ptype) return RunResult::failed("if: missing predicate.type");
        auto* p = reg_.resolvePredicate(ptype);
        if (!p) return RunResult::failed(std::string("unknown predicate: ") + ptype);
        bool ok = p->test(pp["params"], ctx);
        const std::string slot = ok ? "then" : "else";
        auto it = n.children.find(slot);
        if (it == n.children.end()) return RunResult::ok();
        return runSlot(it->second, ctx);
    }
    if (n.type == "repeat") {
        int count = n.params["count"] | 1;
        uint32_t intervalMs = n.params["interval_ms"] | 0;
        auto it = n.children.find("body");
        if (it == n.children.end()) return RunResult::ok();
        for (int i = 0; i < count; ++i) {
            auto r = runSlot(it->second, ctx);
            if (r.status == RunStatus::Failed) return r;
            if (intervalMs && i + 1 < count) interpDelay(intervalMs);
        }
        return RunResult::ok();
    }
    auto* b = reg_.resolveBlock(n.type);
    if (!b) return RunResult::failed("unknown block: " + n.type);
    return b->run(n.params, n.children, ctx, *this);
}

}  // namespace seqb
