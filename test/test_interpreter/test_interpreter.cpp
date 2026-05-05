#include <unity.h>
#include "core/Interpreter.h"
#include "core/Registry.h"
#include "fakes/FakeBlock.h"
#include "fakes/FakePredicate.h"

using namespace seqb;

static FakeBlock* wait_;
static FakeBlock* repeatBlock_;
static FakeBlock* ifBlock_;
static FakePredicate* pred_;

void setUp() {
    Registry::reset();
    wait_ = new FakeBlock("wait", "W", "F");
    Registry::instance().registerBlock(wait_);
    pred_ = new FakePredicate("on-wifi");
    Registry::instance().registerPredicate(pred_);
}
void tearDown() {
    delete wait_;
    delete pred_;
}

void test_runs_leaf_block() {
    Interpreter interp(Registry::instance());
    Sequence s;
    Node n;
    n.type = "wait";
    s.nodes.push_back(n);
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_TRUE(wait_->ran);
}

void test_unknown_block_fails() {
    Interpreter interp(Registry::instance());
    Sequence s;
    Node n;
    n.type = "nope";
    s.nodes.push_back(n);
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("nope") != std::string::npos);
}

void test_repeat_runs_body_n_times() {
    // Fresh registry with a counting block registered as "wait".
    Registry::reset();
    struct CountingBlock : public Block {
        BlockSchema sch{"wait", "W", "F", nullptr, 0, nullptr, 0};
        int calls;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            calls++;
            return RunResult::ok();
        }
    };
    static CountingBlock cb;
    cb.calls = 0;
    Registry::instance().registerBlock(&cb);

    Interpreter interp(Registry::instance());
    Sequence s;
    Node rep;
    rep.type = "repeat";
    rep.params["count"] = 3;
    Node leaf;
    leaf.type = "wait";
    rep.children["body"].push_back(leaf);
    s.nodes.push_back(rep);

    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL(3, cb.calls);
}

void test_if_runs_then_when_true() {
    Interpreter interp(Registry::instance());
    Sequence s;
    Node ifn;
    ifn.type = "if";
    ifn.params["predicate"]["type"] = "on-wifi";
    ifn.params["predicate"]["params"]["x"] = 1;
    Node t;
    t.type = "wait";
    ifn.children["then"].push_back(t);
    s.nodes.push_back(ifn);

    pred_->result = true;
    wait_->ran = false;
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_TRUE(wait_->ran);
}

void test_if_runs_else_when_false() {
    Interpreter interp(Registry::instance());
    Sequence s;
    Node ifn;
    ifn.type = "if";
    ifn.params["predicate"]["type"] = "on-wifi";
    Node t;
    t.type = "wait";
    t.params["which"] = "then";
    Node e;
    e.type = "wait";
    e.params["which"] = "else";
    ifn.children["then"].push_back(t);
    ifn.children["else"].push_back(e);
    s.nodes.push_back(ifn);

    pred_->result = false;
    wait_->ran = false;
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_TRUE(wait_->ran);
}

void test_failure_aborts_sequence() {
    Registry::reset();
    struct FailBlock : public Block {
        BlockSchema sch{"fail", "Fail", "F", nullptr, 0, nullptr, 0};
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            return RunResult::failed("boom");
        }
    };
    struct OkBlock : public Block {
        BlockSchema sch{"ok", "Ok", "F", nullptr, 0, nullptr, 0};
        bool* ran;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            *ran = true;
            return RunResult::ok();
        }
    };
    static FailBlock fb;
    static OkBlock ob;
    bool ran2 = false;
    ob.ran = &ran2;
    Registry::instance().registerBlock(&fb);
    Registry::instance().registerBlock(&ob);

    Interpreter interp(Registry::instance());
    Sequence s;
    Node n1;
    n1.type = "fail";
    Node n2;
    n2.type = "ok";
    s.nodes.push_back(n1);
    s.nodes.push_back(n2);
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_FALSE(ran2);
}

void test_param_substitution_in_block_params() {
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

    Interpreter interp(Registry::instance());
    Sequence s;
    s.id = "x";
    ParamDef p;
    p.key = "host";
    p.type = FieldType::String;
    p.defaultValue.set("h.example");
    s.params.push_back(std::move(p));
    Node n;
    n.type = "cap";
    n.params["url"] = "http://${host}/";
    s.nodes.push_back(n);

    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("http://h.example/", cb.seen["url"].as<const char*>());
}

void test_args_override_default() {
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

    Sequence s;
    ParamDef p;
    p.key = "host";
    p.type = FieldType::String;
    p.defaultValue.set("default");
    s.params.push_back(std::move(p));
    Node n;
    n.type = "cap";
    n.params["v"] = "${host}";
    s.nodes.push_back(n);

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    JsonDocument args;
    args["host"] = "override";
    auto r = interp.runSequence(s, ctx, args.as<JsonVariantConst>());
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("override", cb.seen["v"].as<const char*>());
}

void test_unknown_param_fails_run() {
    Registry::reset();
    static FakeBlock cap("cap", "C", "F");
    Registry::instance().registerBlock(&cap);
    Sequence s;
    Node n;
    n.type = "cap";
    n.params["x"] = "${nope}";
    s.nodes.push_back(n);
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("nope") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_runs_leaf_block);
    RUN_TEST(test_unknown_block_fails);
    RUN_TEST(test_repeat_runs_body_n_times);
    RUN_TEST(test_if_runs_then_when_true);
    RUN_TEST(test_if_runs_else_when_false);
    RUN_TEST(test_failure_aborts_sequence);
    RUN_TEST(test_param_substitution_in_block_params);
    RUN_TEST(test_args_override_default);
    RUN_TEST(test_unknown_param_fails_run);
    return UNITY_END();
}
