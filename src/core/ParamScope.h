#pragma once
#include <ArduinoJson.h>
#include <map>
#include <string>

namespace seqb {

// One activation frame's parameter values, keyed by ParamDef.key.
// JsonDocument owns the value (string/int/bool/array etc.).
struct ParamScope {
    std::map<std::string, JsonDocument> values;
};

}  // namespace seqb
