#include <unity.h>
#include "core/TriggerManager.h"
#include "core/Registry.h"
#include "fakes/FakeTrigger.h"

using namespace seqb;

void setUp() {
    Registry::reset();
}
void tearDown() {}

void test_binds_on_apply() {
    static FakeTrigger ht("http-route");
    Registry::instance().registerTrigger(&ht);
    std::vector<TriggerBinding> bs;
    TriggerBinding b;
    b.id = "b1";
    b.type = "http-route";
    b.sequenceId = "s1";
    b.enabled = true;
    bs.push_back(b);

    int fired = 0;
    std::string firedSeq;
    TriggerManager tm(Registry::instance(), [&](const std::string& sid) {
        fired++;
        firedSeq = sid;
    });
    tm.applyBindings(bs);

    TEST_ASSERT_EQUAL(1, (int)ht.bindings.size());
    ht.fire("b1");
    TEST_ASSERT_EQUAL(1, fired);
    TEST_ASSERT_EQUAL_STRING("s1", firedSeq.c_str());
}

void test_apply_unbinds_old_then_rebinds() {
    static FakeTrigger ht("http-route");
    Registry::instance().registerTrigger(&ht);

    TriggerBinding b1;
    b1.id = "b1";
    b1.type = "http-route";
    b1.sequenceId = "s1";
    b1.enabled = true;
    TriggerBinding b2;
    b2.id = "b2";
    b2.type = "http-route";
    b2.sequenceId = "s2";
    b2.enabled = true;

    TriggerManager tm(Registry::instance(), [](const std::string&) {});
    tm.applyBindings({b1});
    TEST_ASSERT_EQUAL(1, (int)ht.bindings.size());
    tm.applyBindings({b2});
    TEST_ASSERT_EQUAL(1, (int)ht.bindings.size());
    TEST_ASSERT_TRUE(ht.bindings.count("b2") == 1);
    TEST_ASSERT_TRUE(ht.bindings.count("b1") == 0);
}

void test_disabled_binding_not_bound() {
    static FakeTrigger ht("http-route");
    Registry::instance().registerTrigger(&ht);
    TriggerBinding b;
    b.id = "b1";
    b.type = "http-route";
    b.sequenceId = "s1";
    b.enabled = false;
    TriggerManager tm(Registry::instance(), [](const std::string&) {});
    tm.applyBindings({b});
    TEST_ASSERT_EQUAL(0, (int)ht.bindings.size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_binds_on_apply);
    RUN_TEST(test_apply_unbinds_old_then_rebinds);
    RUN_TEST(test_disabled_binding_not_bound);
    return UNITY_END();
}
