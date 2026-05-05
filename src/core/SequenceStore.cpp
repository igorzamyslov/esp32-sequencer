#include "SequenceStore.h"
#include "SequenceCodec.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {

void walkNodes(std::vector<seqb::Node>& nodes, const std::function<void(seqb::Node&)>& visit) {
    for (auto& n : nodes) {
        visit(n);
        for (auto& kv : n.children)
            walkNodes(kv.second, visit);
    }
}

void validateCallSequenceArgs(std::vector<seqb::Sequence>& all) {
    auto findCallee = [&](const std::string& id) -> const seqb::Sequence* {
        auto it = std::find_if(
            all.begin(), all.end(), [&id](const seqb::Sequence& s) { return s.id == id; });
        return it == all.end() ? nullptr : &*it;
    };
    for (auto& caller : all) {
        if (caller.broken) continue;
        walkNodes(caller.nodes, [&](seqb::Node& n) {
            if (caller.broken) return;  // early-exit
            if (n.type != "call-sequence") return;
            const char* cid = n.params["sequenceId"].as<const char*>();
            if (!cid) {
                caller.broken = true;
                caller.brokenReason = "call-sequence: missing sequenceId";
                return;
            }
            const seqb::Sequence* callee = findCallee(cid);
            if (!callee) {
                caller.broken = true;
                caller.brokenReason = std::string("unknown sequence: ") + cid;
                return;
            }
            JsonVariantConst args = n.params["args"];
            for (const auto& pd : callee->params) {
                if (!pd.required) continue;
                if (!pd.defaultValue.isNull()) continue;
                if (args.isNull() || !args[pd.key.c_str()].is<JsonVariantConst>()) {
                    caller.broken = true;
                    caller.brokenReason = "call-sequence to " + std::string(cid) +
                                          " missing required arg '" + pd.key + "'";
                    return;
                }
            }
        });
    }
}

}  // namespace

namespace seqb {

static const char* KEY = "sequences";

void SequenceStore::load() {
    auto blob = p_.load(KEY);
    seqs_ = SequenceCodec::decodeList(blob.empty() ? "[]" : blob.c_str());
    validateCallSequenceArgs(seqs_);
}

void SequenceStore::save() {
    p_.save(KEY, SequenceCodec::encodeList(seqs_));
}

std::string SequenceStore::newId() {
    char buf[9];
    static const char* h = "0123456789abcdef";
    for (int i = 0; i < 8; ++i)
        buf[i] = h[std::rand() & 0xf];
    buf[8] = 0;
    return std::string(buf);
}

void SequenceStore::replaceAll(std::vector<Sequence> seqs) {
    for (auto& s : seqs)
        if (s.id.empty()) s.id = newId();
    seqs_ = std::move(seqs);
    validateCallSequenceArgs(seqs_);
}

const Sequence* SequenceStore::findById(const std::string& id) const {
    auto it =
        std::find_if(seqs_.begin(), seqs_.end(), [&id](const Sequence& s) { return s.id == id; });
    return it == seqs_.end() ? nullptr : &*it;
}

}  // namespace seqb
