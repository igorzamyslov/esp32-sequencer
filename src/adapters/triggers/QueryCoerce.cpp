#include "QueryCoerce.h"
#include <cstdlib>
#include <cstring>

namespace seqb {

namespace {
std::string lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c += 32;
    return out;
}

bool parseIntStrict(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = (int)v;
    return true;
}

bool enumContains(const std::string& csv, const std::string& v) {
    size_t i = 0;
    while (i <= csv.size()) {
        size_t j = csv.find(',', i);
        if (j == std::string::npos) j = csv.size();
        if (csv.compare(i, j - i, v) == 0) return true;
        i = j + 1;
    }
    return false;
}
}  // namespace

CoerceResult coerceQueryArgs(JsonVariantConst defaultArgs,
                             const std::vector<ParamDef>& params,
                             const std::map<std::string, std::string>& query) {
    CoerceResult r;
    JsonObject obj = r.doc.to<JsonObject>();

    // First: seed defaults from ParamDef.defaultValue
    for (const auto& pd : params) {
        if (!pd.defaultValue.isNull()) {
            obj[pd.key.c_str()].set(pd.defaultValue.as<JsonVariantConst>());
        }
    }

    // Second: overlay defaultArgs (binding-time overrides, higher priority than ParamDef defaults)
    if (!defaultArgs.isNull() && defaultArgs.is<JsonObjectConst>()) {
        for (JsonPairConst kv : defaultArgs.as<JsonObjectConst>()) {
            obj[kv.key()].set(kv.value());
        }
    }

    // Third: apply query overrides (highest priority), coercing types
    for (const auto& pd : params) {
        auto it = query.find(pd.key);
        if (it == query.end()) continue;
        const std::string& v = it->second;

        switch (pd.type) {
            case FieldType::String:
            case FieldType::MacAddress:
                obj[pd.key.c_str()] = v.c_str();
                break;
            case FieldType::StringList: {
                JsonArray a = obj[pd.key.c_str()].to<JsonArray>();
                size_t i = 0;
                while (i <= v.size()) {
                    size_t j = v.find(',', i);
                    if (j == std::string::npos) j = v.size();
                    a.add(v.substr(i, j - i).c_str());
                    i = j + 1;
                }
                break;
            }
            case FieldType::Int: {
                int n;
                if (!parseIntStrict(v, n)) {
                    r.ok = false;
                    r.error = "invalid param '" + pd.key + "': not int";
                    return r;
                }
                obj[pd.key.c_str()] = n;
                break;
            }
            case FieldType::Bool: {
                std::string l = lower(v);
                if (l == "1" || l == "true" || l == "yes" || l == "on")
                    obj[pd.key.c_str()] = true;
                else if (l == "0" || l == "false" || l == "no" || l == "off")
                    obj[pd.key.c_str()] = false;
                else {
                    r.ok = false;
                    r.error = "invalid param '" + pd.key + "': not bool";
                    return r;
                }
                break;
            }
            case FieldType::Enum: {
                if (!enumContains(pd.enumValues, v)) {
                    r.ok = false;
                    r.error = "invalid param '" + pd.key + "': not in enum";
                    return r;
                }
                obj[pd.key.c_str()] = v.c_str();
                break;
            }
            case FieldType::PredicateRef:
                // Not overridable via query — ignore silently.
                break;
        }
    }
    return r;
}

}  // namespace seqb
