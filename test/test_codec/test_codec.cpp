#include <unity.h>
#include "core/SequenceCodec.h"
#include "core/Registry.h"
#include "fakes/FakeBlock.h"

using namespace seqb;

void setUp() {
    Registry::reset();
    static FakeBlock wait("wait", "Wait", "Flow");
    static FakeBlock repeat("repeat", "Repeat", "Flow");
    Registry::instance().registerBlock(&wait);
    Registry::instance().registerBlock(&repeat);
}
void tearDown() {}

void test_decode_simple_sequence() {
    const char* json = R"([
      { "id":"abc", "name":"S", "cooldownMs":30000,
        "nodes":[ {"type":"wait","params":{"ms":500},"children":{}} ] }
    ])";
    auto seqs = SequenceCodec::decodeList(json);
    TEST_ASSERT_EQUAL(1, (int)seqs.size());
    TEST_ASSERT_EQUAL_STRING("abc", seqs[0].id.c_str());
    TEST_ASSERT_EQUAL(30000, seqs[0].cooldownMs);
    TEST_ASSERT_EQUAL(1, (int)seqs[0].nodes.size());
    TEST_ASSERT_EQUAL_STRING("wait", seqs[0].nodes[0].type.c_str());
    TEST_ASSERT_EQUAL(500, seqs[0].nodes[0].params["ms"].as<int>());
}

void test_decode_nested_children() {
    const char* json = R"([{ "id":"x","name":"X","nodes":[
      {"type":"repeat","params":{"count":3},"children":{
         "body":[ {"type":"wait","params":{"ms":10},"children":{}} ]
      }}
    ]}])";
    auto seqs = SequenceCodec::decodeList(json);
    TEST_ASSERT_EQUAL_STRING("repeat", seqs[0].nodes[0].type.c_str());
    auto& body = seqs[0].nodes[0].children["body"];
    TEST_ASSERT_EQUAL(1, (int)body.size());
    TEST_ASSERT_EQUAL_STRING("wait", body[0].type.c_str());
}

void test_decode_unknown_block_marks_broken() {
    const char* json = R"([{ "id":"y","name":"Y","nodes":[
      {"type":"unknown-block","params":{},"children":{}}
    ]}])";
    auto seqs = SequenceCodec::decodeList(json);
    TEST_ASSERT_EQUAL(1, (int)seqs.size());
    TEST_ASSERT_TRUE(seqs[0].broken);
    TEST_ASSERT_TRUE(seqs[0].brokenReason.find("unknown-block") != std::string::npos);
}

void test_encode_round_trip() {
    Sequence s;
    s.id = "id1";
    s.name = "rt";
    s.cooldownMs = 1000;
    Node n;
    n.type = "wait";
    n.params["ms"] = 250;
    s.nodes.push_back(n);
    std::vector<Sequence> in = {s};
    std::string j = SequenceCodec::encodeList(in);
    auto out = SequenceCodec::decodeList(j.c_str());
    TEST_ASSERT_EQUAL(1, (int)out.size());
    TEST_ASSERT_EQUAL_STRING("id1", out[0].id.c_str());
    TEST_ASSERT_EQUAL(250, out[0].nodes[0].params["ms"].as<int>());
}

void test_decode_triggers_list() {
    const char* json = R"([{
      "id":"t1","type":"http-route","params":{"path":"/play"},
      "sequenceId":"abc","enabled":true
    }])";
    auto trigs = SequenceCodec::decodeTriggers(json);
    TEST_ASSERT_EQUAL(1, (int)trigs.size());
    TEST_ASSERT_EQUAL_STRING("http-route", trigs[0].type.c_str());
    TEST_ASSERT_EQUAL_STRING("abc", trigs[0].sequenceId.c_str());
    TEST_ASSERT_TRUE(trigs[0].enabled);
    TEST_ASSERT_EQUAL_STRING("/play", trigs[0].params["path"].as<const char*>());
}

void test_decode_sequence_with_params() {
    const char* json = R"([
      { "id":"p1","name":"P","cooldownMs":1000,
        "params":[
          {"key":"hostMac","type":"mac","label":"Host MAC","default":"AA:BB:CC:DD:EE:FF","required":true},
          {"key":"steps","type":"int","label":"Steps","default":3},
          {"key":"loud","type":"bool","label":"Loud","default":true},
          {"key":"src","type":"enum","label":"Src","default":"hdmi3","enumValues":"hdmi1,hdmi2,hdmi3"}
        ],
        "nodes":[ {"type":"wait","params":{"ms":50},"children":{}} ]
      }
    ])";
    auto seqs = SequenceCodec::decodeList(json);
    TEST_ASSERT_EQUAL(1, (int)seqs.size());
    TEST_ASSERT_EQUAL(4, (int)seqs[0].params.size());
    TEST_ASSERT_EQUAL_STRING("hostMac", seqs[0].params[0].key.c_str());
    TEST_ASSERT_EQUAL((int)FieldType::MacAddress, (int)seqs[0].params[0].type);
    TEST_ASSERT_TRUE(seqs[0].params[0].required);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", seqs[0].params[0].defaultValue.as<const char*>());
    TEST_ASSERT_EQUAL(3, seqs[0].params[1].defaultValue.as<int>());
    TEST_ASSERT_TRUE(seqs[0].params[2].defaultValue.as<bool>());
    TEST_ASSERT_EQUAL_STRING("hdmi1,hdmi2,hdmi3", seqs[0].params[3].enumValues.c_str());
}

void test_encode_sequence_with_params_round_trip() {
    Sequence s;
    s.id = "id";
    s.name = "x";
    s.cooldownMs = 60000;
    ParamDef p;
    p.key = "n";
    p.type = FieldType::Int;
    p.label = "N";
    p.defaultValue.set(7);
    p.required = false;
    s.params.push_back(std::move(p));
    Node n;
    n.type = "wait";
    n.params["ms"] = 10;
    s.nodes.push_back(n);
    auto j = SequenceCodec::encodeList({s});
    auto out = SequenceCodec::decodeList(j.c_str());
    TEST_ASSERT_EQUAL(1, (int)out[0].params.size());
    TEST_ASSERT_EQUAL_STRING("n", out[0].params[0].key.c_str());
    TEST_ASSERT_EQUAL(7, out[0].params[0].defaultValue.as<int>());
    TEST_ASSERT_EQUAL((int)FieldType::Int, (int)out[0].params[0].type);
    TEST_ASSERT_FALSE(out[0].params[0].required);
}

void test_decode_trigger_with_args() {
    const char* json = R"([{
      "id":"t","type":"http-route","params":{"path":"/play"},
      "sequenceId":"abc","enabled":true,
      "args":{"hostMac":"AA:BB:CC:DD:EE:FF","steps":3}
    }])";
    auto trigs = SequenceCodec::decodeTriggers(json);
    TEST_ASSERT_EQUAL(1, (int)trigs.size());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", trigs[0].args["hostMac"].as<const char*>());
    TEST_ASSERT_EQUAL(3, trigs[0].args["steps"].as<int>());
}

void test_encode_trigger_with_args_round_trip() {
    TriggerBinding b;
    b.id = "t";
    b.type = "http-route";
    b.sequenceId = "s";
    b.enabled = true;
    b.params["path"] = "/x";
    b.args["k"] = "v";
    auto j = SequenceCodec::encodeTriggers({b});
    auto out = SequenceCodec::decodeTriggers(j.c_str());
    TEST_ASSERT_EQUAL_STRING("v", out[0].args["k"].as<const char*>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_simple_sequence);
    RUN_TEST(test_decode_nested_children);
    RUN_TEST(test_decode_unknown_block_marks_broken);
    RUN_TEST(test_encode_round_trip);
    RUN_TEST(test_decode_triggers_list);
    RUN_TEST(test_decode_sequence_with_params);
    RUN_TEST(test_encode_sequence_with_params_round_trip);
    RUN_TEST(test_decode_trigger_with_args);
    RUN_TEST(test_encode_trigger_with_args_round_trip);
    return UNITY_END();
}
