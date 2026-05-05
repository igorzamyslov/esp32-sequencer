#pragma once
#include "Block.h"
#include "Predicate.h"
#include "Trigger.h"
#include <map>
#include <string>

namespace seqb {

class Registry {
public:
    static Registry& instance();
    static void reset();   // tests only

    void registerBlock(Block* b);
    void registerPredicate(Predicate* p);
    void registerTrigger(Trigger* t);

    Block*     resolveBlock(const std::string& type) const;
    Predicate* resolvePredicate(const std::string& type) const;
    Trigger*   resolveTrigger(const std::string& type) const;

    const std::map<std::string, Block*>&     blocks()     const { return blocks_; }
    const std::map<std::string, Predicate*>& predicates() const { return predicates_; }
    const std::map<std::string, Trigger*>&   triggers()   const { return triggers_; }

    // Serialise all schemas to JSON for /api/schema
    std::string dumpSchemaJson() const;

private:
    std::map<std::string, Block*>     blocks_;
    std::map<std::string, Predicate*> predicates_;
    std::map<std::string, Trigger*>   triggers_;
};

}  // namespace seqb
