#include <unity.h>
#include "core/ResolveParams.h"

using namespace seqb;

void setUp() {}
void tearDown() {}

static ParamScope makeScope() {
    ParamScope s;
    JsonDocument a;
    a.set("AA:BB:CC:DD:EE:FF");
    s.values["mac"] = std::move(a);
    JsonDocument b;
    b.set(7);
    s.values["n"] = std::move(b);
    JsonDocument c;
    c.set(true);
    s.values["loud"] = std::move(c);
    JsonDocument d;
    d.set("hdmi3");
    s.values["src"] = std::move(d);
    return s;
}

void test_string_placeholder_full() {
    JsonDocument in;
    in.set("${mac}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", r.doc.as<const char*>());
}

void test_string_placeholder_interpolation() {
    JsonDocument in;
    in.set("/api/${src}/light/${n}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("/api/hdmi3/light/7", r.doc.as<const char*>());
}

void test_object_param_typed_int() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "n";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(7, r.doc.as<int>());
}

void test_object_param_typed_bool() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "loud";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.doc.as<bool>());
}

void test_object_recurse_keeps_keys() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["mac"] = "${mac}";
    o["count"]["$param"] = "n";
    o["literal"] = 42;
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", r.doc["mac"].as<const char*>());
    TEST_ASSERT_EQUAL(7, r.doc["count"].as<int>());
    TEST_ASSERT_EQUAL(42, r.doc["literal"].as<int>());
}

void test_array_recurse() {
    JsonDocument in;
    JsonArray a = in.to<JsonArray>();
    a.add("KEY_${src}");
    a.add(1);
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("KEY_hdmi3", r.doc[0].as<const char*>());
    TEST_ASSERT_EQUAL(1, r.doc[1].as<int>());
}

void test_unknown_param_string() {
    JsonDocument in;
    in.set("${nope}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("nope") != std::string::npos);
}

void test_unknown_param_object() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "missing";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("missing") != std::string::npos);
}

void test_literal_passthrough() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["x"] = 1;
    o["y"] = "plain";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(1, r.doc["x"].as<int>());
    TEST_ASSERT_EQUAL_STRING("plain", r.doc["y"].as<const char*>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_string_placeholder_full);
    RUN_TEST(test_string_placeholder_interpolation);
    RUN_TEST(test_object_param_typed_int);
    RUN_TEST(test_object_param_typed_bool);
    RUN_TEST(test_object_recurse_keeps_keys);
    RUN_TEST(test_array_recurse);
    RUN_TEST(test_unknown_param_string);
    RUN_TEST(test_unknown_param_object);
    RUN_TEST(test_literal_passthrough);
    return UNITY_END();
}
