#pragma once
#include <string>
namespace seqb {
class Persistence {
public:
    virtual ~Persistence() = default;
    // load: returns "" if missing.
    virtual std::string load(const std::string& key) = 0;
    virtual void save(const std::string& key, const std::string& blob) = 0;
};
}  // namespace seqb
