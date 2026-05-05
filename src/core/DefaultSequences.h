#pragma once
#include "Sequence.h"
#include <vector>

namespace seqb {

struct DefaultsResult {
    std::vector<Sequence> sequences;
    std::vector<TriggerBinding> triggers;
};

// Build a starter sequence + http-route binding with placeholder values.
// Users edit MACs/IPs in the editor on first run.
DefaultsResult buildDefaults();

}  // namespace seqb
