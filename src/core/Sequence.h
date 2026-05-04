#pragma once
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

struct Sequence {
    std::string id;             // 8-char hex, server-assigned
    std::string name;
    uint32_t    cooldownMs = 60000;
    std::vector<Node> nodes;
    bool broken = false;        // true if a child references an unknown block type
    std::string brokenReason;
};

struct TriggerBinding {
    std::string id;
    std::string type;
    JsonDocument params;
    std::string sequenceId;
    bool enabled = true;
};

}  // namespace seqb
