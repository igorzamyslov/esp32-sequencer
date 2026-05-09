#include <unity.h>
#include "adapters/triggers/QueryCoerce.h"

using namespace seqb;

void setUp() {}
void tearDown() {}

static std::vector<ParamDef> mkParams() {
    std::vector<ParamDef> ps;
    auto add = [&](const char* k, FieldType t, const char* def, const char* enums = nullptr) {
        ParamDef p;
        p.key = k;
        p.type = t;
        if (def) p.defaultValue.set(def);
        if (enums) p.enumValues = enums;
        ps.push_back(std::move(p));
    };
    add("host", FieldType::String, "h.local");
    add("n", FieldType::Int, nullptr);
    ps.back().defaultValue.set(3);
    add("loud", FieldType::Bool, nullptr);
    ps.back().defaultValue.set(false);
    add("src", FieldType::Enum, "hdmi1", "hdmi1,hdmi2,hdmi3");
    add("keys", FieldType::StringList, nullptr);
    return ps;
}

void test_defaults_when_no_query() {
    auto ps = mkParams();
    auto r = coerceQueryArgs(JsonVariantConst(), ps, {});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("h.local", r.doc["host"].as<const char*>());
    TEST_ASSERT_EQUAL(3, r.doc["n"].as<int>());
    TEST_ASSERT_FALSE(r.doc["loud"].as<bool>());
}

void test_query_override_string() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"host", "other"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("other", r.doc["host"].as<const char*>());
}

void test_query_override_int_ok() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"n", "42"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(42, r.doc["n"].as<int>());
}

void test_query_override_int_fail() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"n", "oops"}});
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("'n'") != std::string::npos);
}

void test_query_override_bool() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud", "yes"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.doc["loud"].as<bool>());
    auto r2 = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud", "off"}});
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_FALSE(r2.doc["loud"].as<bool>());
    auto r3 = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud", "maybe"}});
    TEST_ASSERT_FALSE(r3.ok);
}

void test_query_override_enum() {
    auto ok = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"src", "hdmi3"}});
    TEST_ASSERT_TRUE(ok.ok);
    TEST_ASSERT_EQUAL_STRING("hdmi3", ok.doc["src"].as<const char*>());
    auto bad = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"src", "hdmi9"}});
    TEST_ASSERT_FALSE(bad.ok);
}

void test_query_override_stringlist() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"keys", "A,B,C"}});
    TEST_ASSERT_TRUE(r.ok);
    auto a = r.doc["keys"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL(3, (int)a.size());
    TEST_ASSERT_EQUAL_STRING("A", a[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("C", a[2].as<const char*>());
}

void test_unknown_query_key_ignored() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"unknown", "x"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.doc["unknown"].is<JsonVariantConst>());
}

void test_default_args_overlaid() {
    JsonDocument def;
    def["host"] = "fromBinding";
    def["n"] = 99;
    auto r = coerceQueryArgs(def.as<JsonVariantConst>(), mkParams(), {{"n", "5"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("fromBinding", r.doc["host"].as<const char*>());
    TEST_ASSERT_EQUAL(5, r.doc["n"].as<int>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_when_no_query);
    RUN_TEST(test_query_override_string);
    RUN_TEST(test_query_override_int_ok);
    RUN_TEST(test_query_override_int_fail);
    RUN_TEST(test_query_override_bool);
    RUN_TEST(test_query_override_enum);
    RUN_TEST(test_query_override_stringlist);
    RUN_TEST(test_unknown_query_key_ignored);
    RUN_TEST(test_default_args_overlaid);
    return UNITY_END();
}
