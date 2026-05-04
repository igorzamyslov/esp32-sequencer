#pragma once
#include "Sequence.h"
#include <vector>

namespace seqb {

struct DefaultsResult {
    std::vector<Sequence> sequences;
    std::vector<TriggerBinding> triggers;
};

// Build the default seed using runtime config values (so MACs match user config).
DefaultsResult buildDefaults(const std::string& pcMac,
                             const std::string& tvMac,
                             const std::string& dualsenseMac);

}
