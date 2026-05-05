#include "TriggerManager.h"
#include "Registry.h"

namespace seqb {

TriggerManager::TriggerManager(Registry& r, FireCallback cb) : reg_(r), cb_(std::move(cb)) {}

void TriggerManager::applyBindings(const std::vector<TriggerBinding>& bs) {
    for (auto& kv : active_)
        kv.second->unbind(kv.first);
    active_.clear();
    anyPauses_ = false;
    for (auto& b : bs) {
        if (!b.enabled) continue;
        auto* t = reg_.resolveTrigger(b.type);
        if (!t) continue;
        t->bind(b.id, b.params.as<JsonVariantConst>(), b.sequenceId, cb_);
        active_[b.id] = t;
        if (t->schema().pausesBleScan) anyPauses_ = true;
    }
}

bool TriggerManager::anyPausesBleScan() const {
    return anyPauses_;
}

}  // namespace seqb
