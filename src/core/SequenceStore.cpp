#include "SequenceStore.h"
#include "SequenceCodec.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace seqb {

static const char* KEY = "sequences";

void SequenceStore::load() {
    auto blob = p_.load(KEY);
    seqs_ = SequenceCodec::decodeList(blob.empty() ? "[]" : blob.c_str());
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
}

const Sequence* SequenceStore::findById(const std::string& id) const {
    auto it =
        std::find_if(seqs_.begin(), seqs_.end(), [&id](const Sequence& s) { return s.id == id; });
    return it == seqs_.end() ? nullptr : &*it;
}

}  // namespace seqb
