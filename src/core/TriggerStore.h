#pragma once
#include "Persistence.h"
#include "Sequence.h"
#include <vector>
namespace seqb {
class TriggerStore {
public:
    explicit TriggerStore(Persistence& p) : p_(p) {}
    void load();
    void save();
    void replaceAll(std::vector<TriggerBinding> ts);
    const std::vector<TriggerBinding>& all() const { return ts_; }

private:
    Persistence& p_;
    std::vector<TriggerBinding> ts_;
};
}  // namespace seqb
