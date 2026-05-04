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
    s.id = "id1"; s.name = "rt"; s.cooldownMs = 1000;
    Node n; n.type = "wait"; n.params["ms"] = 250;
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_simple_sequence);
    RUN_TEST(test_decode_nested_children);
    RUN_TEST(test_decode_unknown_block_marks_broken);
    RUN_TEST(test_encode_round_trip);
    RUN_TEST(test_decode_triggers_list);
    return UNITY_END();
}
