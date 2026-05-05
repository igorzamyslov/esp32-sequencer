#include "TriggerStore.h"
#include "SequenceCodec.h"
#include "SequenceStore.h"  // for newId

namespace seqb {

static const char* KEY = "triggers";

void TriggerStore::load() {
    auto blob = p_.load(KEY);
    ts_ = SequenceCodec::decodeTriggers(blob.empty() ? "[]" : blob.c_str());
}
void TriggerStore::save() {
    p_.save(KEY, SequenceCodec::encodeTriggers(ts_));
}
void TriggerStore::replaceAll(std::vector<TriggerBinding> ts) {
    for (auto& b : ts) if (b.id.empty()) b.id = SequenceStore::newId();
    ts_ = std::move(ts);
}

}
