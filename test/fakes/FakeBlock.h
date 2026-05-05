#pragma once
#include "core/Block.h"
namespace seqb {
class FakeBlock : public Block {
public:
    FakeBlock(const char* t, const char* l, const char* c) {
        schema_ = {t, l, c, nullptr, 0, nullptr, 0};
    }
    const BlockSchema& schema() const override { return schema_; }
    RunResult run(JsonVariantConst,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        ran = true;
        return result;
    }
    bool ran = false;
    RunResult result = RunResult::ok();

private:
    BlockSchema schema_;
};
}  // namespace seqb
