#pragma once
#include "Sequence.h"
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace seqb {
class SequenceCodec {
public:
    static std::vector<Sequence> decodeList(const char* json);
    static std::vector<Sequence> decodeList(JsonVariantConst v);
    static std::string encodeList(const std::vector<Sequence>& seqs);
    static std::vector<TriggerBinding> decodeTriggers(const char* json);
    static std::vector<TriggerBinding> decodeTriggers(JsonVariantConst v);
    static std::string encodeTriggers(const std::vector<TriggerBinding>& ts);
};
}  // namespace seqb
