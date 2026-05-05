#include <unity.h>
#include "core/Interpreter.h"
#include "core/Registry.h"
#include "fakes/FakeBlock.h"

using namespace seqb;

static FakeBlock* leaf_;

void setUp() {
    Registry::reset();
    leaf_ = new FakeBlock("leaf", "L", "F");
    Registry::instance().registerBlock(leaf_);
}
void tearDown() {
    delete leaf_;
}

static Sequence makeCallee(const char* id) {
    Sequence s;
    s.id = id;
    s.name = id;
    Node n;
    n.type = "leaf";
    s.nodes.push_back(n);
    return s;
}

static const Sequence* lookup(const std::vector<const Sequence*>& list, const std::string& id) {
    for (auto* s : list)
        if (s->id == id) return s;
    return nullptr;
}

void test_call_sequence_runs_callee() {
    auto callee = makeCallee("CB");
    std::vector<const Sequence*> all{&callee};

    Sequence caller;
    caller.id = "CA";
    Node call;
    call.type = "call-sequence";
    call.params["sequenceId"] = "CB";
    caller.nodes.push_back(call);

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id) { return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_TRUE(leaf_->ran);
}

void test_call_sequence_args_resolved_against_caller_scope() {
    Registry::reset();
    struct CapturingBlock : public Block {
        BlockSchema sch{"cap", "C", "F", nullptr, 0, nullptr, 0};
        JsonDocument seen;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst params,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            seen.set(params);
            return RunResult::ok();
        }
    };
    static CapturingBlock cb;
    Registry::instance().registerBlock(&cb);

    Sequence callee;
    callee.id = "CB";
    ParamDef p;
    p.key = "x";
    p.type = FieldType::String;
    callee.params.push_back(std::move(p));
    Node leaf;
    leaf.type = "cap";
    leaf.params["v"] = "${x}";
    callee.nodes.push_back(leaf);

    Sequence caller;
    caller.id = "CA";
    ParamDef cp;
    cp.key = "who";
    cp.type = FieldType::String;
    cp.defaultValue.set("alice");
    caller.params.push_back(std::move(cp));
    Node call;
    call.type = "call-sequence";
    call.params["sequenceId"] = "CB";
    call.params["args"]["x"] = "${who}";
    caller.nodes.push_back(call);

    std::vector<const Sequence*> all{&callee};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id) { return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("alice", cb.seen["v"].as<const char*>());
}

void test_call_sequence_cycle_detected() {
    Sequence a;
    a.id = "A";
    Node ca;
    ca.type = "call-sequence";
    ca.params["sequenceId"] = "B";
    a.nodes.push_back(ca);
    Sequence b;
    b.id = "B";
    Node cb;
    cb.type = "call-sequence";
    cb.params["sequenceId"] = "A";
    b.nodes.push_back(cb);
    std::vector<const Sequence*> all{&a, &b};

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id) { return lookup(all, id); };
    auto r = interp.runSequence(a, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("cycle") != std::string::npos);
}

void test_call_sequence_unknown_id_fails() {
    Sequence a;
    a.id = "A";
    Node ca;
    ca.type = "call-sequence";
    ca.params["sequenceId"] = "GHOST";
    a.nodes.push_back(ca);
    std::vector<const Sequence*> all{&a};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id) { return lookup(all, id); };
    auto r = interp.runSequence(a, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("GHOST") != std::string::npos);
}

void test_call_sequence_broken_callee_fails() {
    Sequence callee;
    callee.id = "C";
    callee.broken = true;
    callee.brokenReason = "bad";
    Sequence caller;
    caller.id = "A";
    Node call;
    call.type = "call-sequence";
    call.params["sequenceId"] = "C";
    caller.nodes.push_back(call);
    std::vector<const Sequence*> all{&callee, &caller};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id) { return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("broken") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_call_sequence_runs_callee);
    RUN_TEST(test_call_sequence_args_resolved_against_caller_scope);
    RUN_TEST(test_call_sequence_cycle_detected);
    RUN_TEST(test_call_sequence_unknown_id_fails);
    RUN_TEST(test_call_sequence_broken_callee_fails);
    return UNITY_END();
}
