#pragma once
#include "core/Trigger.h"
class AsyncWebServer;

namespace seqb {
class HttpRouteTrigger : public Trigger {
public:
    static void setServer(AsyncWebServer* s);
    static HttpRouteTrigger& instance();

    const TriggerSchema& schema() const override;
    void bind(const std::string& bindingId,
              JsonVariantConst params,
              const std::string& sequenceId,
              FireCallback onFire) override;
    void unbind(const std::string& bindingId) override;
};
}  // namespace seqb
