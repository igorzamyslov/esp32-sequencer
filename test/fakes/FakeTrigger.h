#pragma once
#include "core/Trigger.h"
#include <map>
namespace seqb {
class FakeTrigger : public Trigger {
public:
    FakeTrigger(const char* t) { schema_ = {t, t, nullptr, 0, false}; }
    const TriggerSchema& schema() const override { return schema_; }
    void bind(const std::string& id, JsonVariantConst, const std::string& seq,
              FireCallback cb) override {
        bindings[id] = {seq, cb};
    }
    void unbind(const std::string& id) override { bindings.erase(id); }
    void fire(const std::string& id) {
        auto it = bindings.find(id);
        if (it != bindings.end()) it->second.cb(it->second.seq);
    }
    struct B { std::string seq; FireCallback cb; };
    std::map<std::string, B> bindings;
private:
    TriggerSchema schema_;
};
}
