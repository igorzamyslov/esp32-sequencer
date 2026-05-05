#include <unity.h>
#include "core/Registry.h"
#include "fakes/FakeBlock.h"
#include "fakes/FakePredicate.h"
#include "fakes/FakeTrigger.h"

using namespace seqb;

void setUp() {
    Registry::reset();
}
void tearDown() {}

void test_register_and_resolve_block() {
    static FakeBlock b("wait", "Wait", "Flow");
    Registry::instance().registerBlock(&b);
    TEST_ASSERT_EQUAL_PTR(&b, Registry::instance().resolveBlock("wait"));
    TEST_ASSERT_NULL(Registry::instance().resolveBlock("nope"));
}

void test_register_and_resolve_predicate() {
    static FakePredicate p("on-wifi");
    Registry::instance().registerPredicate(&p);
    TEST_ASSERT_EQUAL_PTR(&p, Registry::instance().resolvePredicate("on-wifi"));
}

void test_register_and_resolve_trigger() {
    static FakeTrigger t("http-route");
    Registry::instance().registerTrigger(&t);
    TEST_ASSERT_EQUAL_PTR(&t, Registry::instance().resolveTrigger("http-route"));
}

void test_dump_schema_json_contains_registered_types() {
    static FakeBlock b("wait", "Wait", "Flow");
    static FakePredicate p("on-wifi");
    static FakeTrigger t("http-route");
    Registry::instance().registerBlock(&b);
    Registry::instance().registerPredicate(&p);
    Registry::instance().registerTrigger(&t);
    std::string s = Registry::instance().dumpSchemaJson();
    TEST_ASSERT_TRUE(s.find("\"wait\"") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"on-wifi\"") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"http-route\"") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_register_and_resolve_block);
    RUN_TEST(test_register_and_resolve_predicate);
    RUN_TEST(test_register_and_resolve_trigger);
    RUN_TEST(test_dump_schema_json_contains_registered_types);
    return UNITY_END();
}
