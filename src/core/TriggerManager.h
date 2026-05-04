#pragma once
#include "Trigger.h"
#include "Sequence.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace seqb {

class Registry;

class TriggerManager {
public:
    using FireCallback = std::function<void(const std::string& sequenceId)>;

    TriggerManager(Registry& r, FireCallback cb);
    void applyBindings(const std::vector<TriggerBinding>& bs);
    bool anyPausesBleScan() const;

private:
    Registry& reg_;
    FireCallback cb_;
    // map of bindingId -> Trigger* it was bound to (so we can call unbind)
    std::map<std::string, Trigger*> active_;
    bool anyPauses_ = false;
};

}  // namespace seqb
