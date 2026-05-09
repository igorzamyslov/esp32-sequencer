#include <unity.h>
#include "core/SequenceStore.h"
#include "core/TriggerStore.h"
#include "core/Registry.h"
#include "fakes/FakePersistence.h"
#include "fakes/FakeBlock.h"

using namespace seqb;

static FakePersistence* fp;

void setUp() {
    Registry::reset();
    static FakeBlock w("wait", "Wait", "Flow");
    Registry::instance().registerBlock(&w);
    fp = new FakePersistence();
}
void tearDown() {
    delete fp;
}

void test_store_loads_empty() {
    SequenceStore s(*fp);
    s.load();
    TEST_ASSERT_EQUAL(0, (int)s.all().size());
}

void test_store_save_and_reload() {
    SequenceStore s1(*fp);
    Sequence seq;
    seq.id = "abc";
    seq.name = "x";
    Node n;
    n.type = "wait";
    n.params["ms"] = 100;
    seq.nodes.push_back(n);
    s1.replaceAll({seq});
    s1.save();

    SequenceStore s2(*fp);
    s2.load();
    TEST_ASSERT_EQUAL(1, (int)s2.all().size());
    TEST_ASSERT_EQUAL_STRING("abc", s2.all()[0].id.c_str());
}

void test_store_assigns_id_for_new() {
    SequenceStore s(*fp);
    Sequence seq;
    seq.name = "no-id";  // no id
    s.replaceAll({seq});
    TEST_ASSERT_EQUAL(1, (int)s.all().size());
    TEST_ASSERT_TRUE(!s.all()[0].id.empty());
    TEST_ASSERT_EQUAL(8, (int)s.all()[0].id.size());
}

void test_trigger_store_round_trip() {
    TriggerStore ts(*fp);
    TriggerBinding b;
    b.id = "";
    b.type = "http-route";
    b.sequenceId = "abc";
    b.params["path"] = "/play";
    ts.replaceAll({b});
    ts.save();
    TriggerStore ts2(*fp);
    ts2.load();
    TEST_ASSERT_EQUAL(1, (int)ts2.all().size());
    TEST_ASSERT_EQUAL_STRING("http-route", ts2.all()[0].type.c_str());
    TEST_ASSERT_EQUAL(8, (int)ts2.all()[0].id.size());
}

void test_caller_marked_broken_when_callee_arg_missing() {
    Registry::reset();
    static FakeBlock leaf("leaf", "L", "F");
    Registry::instance().registerBlock(&leaf);

    FakePersistence p;
    SequenceStore store(p);
    Sequence callee;
    callee.id = "CB";
    callee.name = "cb";
    ParamDef pd;
    pd.key = "needed";
    pd.type = FieldType::String;
    pd.required = true;
    callee.params.push_back(std::move(pd));
    Node n;
    n.type = "leaf";
    callee.nodes.push_back(n);

    Sequence caller;
    caller.id = "CA";
    caller.name = "ca";
    Node call;
    call.type = "call-sequence";
    call.params["sequenceId"] = "CB";
    // NOTE: no "args" key -> required arg "needed" is missing
    caller.nodes.push_back(call);

    store.replaceAll({callee, caller});
    auto* loaded = store.findById("CA");
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_TRUE(loaded->broken);
    TEST_ASSERT_TRUE(loaded->brokenReason.find("needed") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_store_loads_empty);
    RUN_TEST(test_store_save_and_reload);
    RUN_TEST(test_store_assigns_id_for_new);
    RUN_TEST(test_trigger_store_round_trip);
    RUN_TEST(test_caller_marked_broken_when_callee_arg_missing);
    return UNITY_END();
}
