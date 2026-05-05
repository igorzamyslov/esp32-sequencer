#include "NvsPersistence.h"
#include <Preferences.h>
#include <Arduino.h>

namespace seqb {

static const char* NS = "seqb";

std::string NvsPersistence::load(const std::string& key) {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return "";
    String s = p.getString(key.c_str(), "");
    p.end();
    return std::string(s.c_str());
}

void NvsPersistence::save(const std::string& key, const std::string& blob) {
    Preferences p;
    p.begin(NS, /*readOnly=*/false);
    p.putString(key.c_str(), blob.c_str());
    p.end();
}

}  // namespace seqb
