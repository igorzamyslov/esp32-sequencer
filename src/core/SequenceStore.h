#pragma once
#include "Persistence.h"
#include "Sequence.h"
#include <vector>
namespace seqb {
class SequenceStore {
public:
    explicit SequenceStore(Persistence& p) : p_(p) {}
    void load();
    void save();
    void replaceAll(std::vector<Sequence> seqs);  // assigns ids if missing
    // Insert a new sequence (assigns id if empty) or replace the existing one
    // with matching id. Returns the id (assigned or pre-existing).
    std::string upsert(Sequence seq);
    bool removeById(const std::string& id);  // true if removed
    const std::vector<Sequence>& all() const { return seqs_; }
    const Sequence* findById(const std::string& id) const;
    static std::string newId();

private:
    Persistence& p_;
    std::vector<Sequence> seqs_;
};
}  // namespace seqb
