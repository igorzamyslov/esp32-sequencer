#pragma once
#include "Schema.h"
#include <ArduinoJson.h>
#include <functional>
#include <string>

namespace seqb {

class Trigger {
public:
    using FireCallback = std::function<void(const std::string& sequenceId)>;
    virtual ~Trigger() = default;
    virtual const TriggerSchema& schema() const = 0;
    // bind: arm this trigger for a binding-id with given params; on fire,
    // invoke onFire(sequenceId). Idempotent if called for the same bindingId.
    virtual void bind(const std::string& bindingId,
                      JsonVariantConst params,
                      const std::string& sequenceId,
                      FireCallback onFire) = 0;
    virtual void unbind(const std::string& bindingId) = 0;
};

}  // namespace seqb
