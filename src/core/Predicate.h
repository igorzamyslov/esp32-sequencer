#pragma once
#include "Schema.h"
#include "Block.h"  // for RunCtx
#include <ArduinoJson.h>

namespace seqb {

class Predicate {
public:
    virtual ~Predicate() = default;
    virtual const PredicateSchema& schema() const = 0;
    virtual bool test(JsonVariantConst params, RunCtx& ctx) = 0;
};

}  // namespace seqb
