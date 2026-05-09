#pragma once
#include "ParamScope.h"
#include <ArduinoJson.h>
#include <string>

namespace seqb {

struct ResolveResult {
    bool ok = true;
    std::string error;
    JsonDocument doc;
};

// Walk `raw`. For every:
//   - String containing "${name}", interpolate scope[name] (stringified for non-strings).
//   - Object that is exactly {"$param":"name"}, replace with scope[name] (typed).
//   - Array/object: recurse.
ResolveResult resolveParams(JsonVariantConst raw, const ParamScope& scope);

}  // namespace seqb
