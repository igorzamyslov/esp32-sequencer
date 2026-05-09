#pragma once
#include "core/Sequence.h"
#include <ArduinoJson.h>
#include <map>
#include <string>

namespace seqb {

struct CoerceResult {
    bool ok = true;
    std::string error;
    JsonDocument doc;
};

// Apply query overrides on top of `defaultArgs`, coerced per `params` types.
// Unknown keys are ignored. Missing required-without-default is NOT enforced
// here (interpreter handles that at run time).
CoerceResult coerceQueryArgs(JsonVariantConst defaultArgs,
                             const std::vector<ParamDef>& params,
                             const std::map<std::string, std::string>& query);

}  // namespace seqb
