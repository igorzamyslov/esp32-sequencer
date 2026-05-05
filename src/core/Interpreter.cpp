#include "Interpreter.h"
#include "Registry.h"
#include "Predicate.h"
#include "ResolveParams.h"

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

ParamScope buildScope(const Sequence& s, JsonVariantConst args) {
    ParamScope sc;
    for (const auto& p : s.params) {
        if (!p.defaultValue.isNull()) sc.values[p.key].set(p.defaultValue.as<JsonVariantConst>());
    }
    if (!args.isNull() && args.is<JsonObjectConst>()) {
        for (JsonPairConst kv : args.as<JsonObjectConst>()) {
            sc.values[std::string(kv.key().c_str())].set(kv.value());
        }
    }
    return sc;
}
}  // namespace

RunResult Interpreter::runSequence(const Sequence& s, RunCtx& ctx, JsonVariantConst args) {
    if ((int)ctx.callStack.size() >= MAX_CALL_DEPTH) {
        return RunResult::failed("call depth exceeded");
    }
    ctx.callStack.push_back(s.id);
    ctx.scopeStack.push_back(buildScope(s, args));
    auto r = runSlot(s.nodes, ctx);
    ctx.scopeStack.pop_back();
    ctx.callStack.pop_back();
    return r;
}

RunResult Interpreter::runSlot(const std::vector<Node>& nodes, RunCtx& ctx) {
    for (const auto& n : nodes) {
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
        if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
        auto pp = n.params["predicate"];
        const char* ptype = pp["type"].as<const char*>();
        if (!ptype) return RunResult::failed("if: missing predicate.type");
        auto* p = reg_.resolvePredicate(ptype);
        if (!p) return RunResult::failed(std::string("unknown predicate: ") + ptype);
        auto resolvedPp = resolveParams(pp["params"].as<JsonVariantConst>(), ctx.scopeStack.back());
        if (!resolvedPp.ok) return RunResult::failed(resolvedPp.error);
        bool ok = p->test(resolvedPp.doc.as<JsonVariantConst>(), ctx);
        const std::string slot = ok ? "then" : "else";
        auto it = n.children.find(slot);
        if (it == n.children.end()) return RunResult::ok();
        return runSlot(it->second, ctx);
    }
    if (n.type == "repeat") {
        if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
        auto resolvedRp = resolveParams(n.params.as<JsonVariantConst>(), ctx.scopeStack.back());
        if (!resolvedRp.ok) return RunResult::failed(resolvedRp.error);
        int count = resolvedRp.doc["count"] | 1;
        // cppcheck-suppress badBitmaskCheck ; ArduinoJson's `|` is its
        // value-or-default operator overload, not bitwise OR.
        uint32_t intervalMs = resolvedRp.doc["interval_ms"] | 0;
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
    if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
    auto resolved = resolveParams(n.params.as<JsonVariantConst>(), ctx.scopeStack.back());
    if (!resolved.ok) return RunResult::failed(resolved.error);
    return b->run(resolved.doc.as<JsonVariantConst>(), n.children, ctx, *this);
}

}  // namespace seqb
