#pragma once
#include "Schema.h"
#include <ArduinoJson.h>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace seqb {

struct Node {
    std::string type;
    JsonDocument params;  // owned; copy-on-set
    std::map<std::string, std::vector<Node>> children;
};

struct ParamDef {
    std::string key;
    FieldType type = FieldType::String;
    std::string label;
    JsonDocument defaultValue;
    std::string enumValues;
    bool required = false;
};

struct Sequence {
    std::string id;  // 8-char hex, server-assigned
    std::string name;
    uint32_t cooldownMs = 60000;
    std::vector<ParamDef> params;
    std::vector<Node> nodes;
    bool broken = false;  // true if a child references an unknown block type
    std::string brokenReason;
};

struct TriggerBinding {
    std::string id;
    std::string type;
    JsonDocument params;
    std::string sequenceId;
    JsonDocument args;
    bool enabled = true;
};

}  // namespace seqb
