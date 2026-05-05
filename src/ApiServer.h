#pragma once
#include <ESPAsyncWebServer.h>
#include "core/SequenceStore.h"
#include "core/TriggerStore.h"
#include "core/TriggerManager.h"
#include "core/Registry.h"
#include <functional>

namespace seqb {

struct ApiHooks {
    std::function<void(const std::string& sequenceId)> enqueueRun;
    std::function<std::string()> currentStatusJson;
};

class ApiServer {
public:
    ApiServer(AsyncWebServer& srv,
              SequenceStore& seqs,
              TriggerStore& trigs,
              TriggerManager& tm,
              ApiHooks hooks);
    void registerRoutes();

private:
    AsyncWebServer& srv_;
    SequenceStore& seqs_;
    TriggerStore& trigs_;
    TriggerManager& tm_;
    ApiHooks hooks_;
};

}  // namespace seqb
