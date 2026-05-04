#pragma once
#include "core/Predicate.h"
namespace seqb {
class FakePredicate : public Predicate {
public:
    FakePredicate(const char* t) { schema_ = {t, t, nullptr, 0}; }
    const PredicateSchema& schema() const override { return schema_; }
    bool test(JsonVariantConst, RunCtx&) override { return result; }
    bool result = true;
private:
    PredicateSchema schema_;
};
}
