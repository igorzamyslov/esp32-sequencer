#pragma once
#include "Sequence.h"
#include <string>
#include <vector>

namespace seqb {
class SequenceCodec {
public:
    static std::vector<Sequence>        decodeList(const char* json);
    static std::string                  encodeList(const std::vector<Sequence>& seqs);
    static std::vector<TriggerBinding>  decodeTriggers(const char* json);
    static std::string                  encodeTriggers(const std::vector<TriggerBinding>& ts);
};
}
