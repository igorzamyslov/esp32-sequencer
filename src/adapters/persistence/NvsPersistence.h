#pragma once
#include "core/Persistence.h"
namespace seqb {
class NvsPersistence : public Persistence {
public:
    std::string load(const std::string& key) override;
    void        save(const std::string& key, const std::string& blob) override;
};
}
