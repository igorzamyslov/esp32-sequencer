#pragma once
#include "core/Persistence.h"
#include <map>
namespace seqb {
class FakePersistence : public Persistence {
public:
    std::map<std::string, std::string> store;
    std::string load(const std::string& k) override {
        auto it = store.find(k);
        return it == store.end() ? "" : it->second;
    }
    void save(const std::string& k, const std::string& v) override { store[k] = v; }
};
}
