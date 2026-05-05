#include "ResolveParams.h"
#include <cstring>

namespace seqb {

namespace {

bool isParamObject(JsonVariantConst v, const char*& nameOut) {
    if (!v.is<JsonObjectConst>()) return false;
    auto o = v.as<JsonObjectConst>();
    if (o.size() != 1) return false;
    auto first = *o.begin();
    if (strcmp(first.key().c_str(), "$param") != 0) return false;
    if (!first.value().is<const char*>()) return false;
    nameOut = first.value().as<const char*>();
    return true;
}

void appendStringified(std::string& out, JsonVariantConst v) {
    if (v.is<const char*>())
        out.append(v.as<const char*>());
    else if (v.is<int>())
        out.append(std::to_string(v.as<int>()));
    else if (v.is<bool>())
        out.append(v.as<bool>() ? "true" : "false");
    else if (v.is<float>())
        out.append(std::to_string(v.as<float>()));
    else {
        std::string j;
        serializeJson(v, j);
        out.append(j);
    }
}

bool interpolate(const char* in, const ParamScope& scope, std::string& out, std::string& err) {
    const char* p = in;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            const char* end = strchr(p + 2, '}');
            if (!end) {
                out.push_back(*p++);
                continue;
            }
            std::string name(p + 2, end - (p + 2));
            auto it = scope.values.find(name);
            if (it == scope.values.end()) {
                err = "unknown param: " + name;
                return false;
            }
            appendStringified(out, it->second.as<JsonVariantConst>());
            p = end + 1;
        } else {
            out.push_back(*p++);
        }
    }
    return true;
}

bool resolveInto(JsonVariantConst src, JsonVariant dst, const ParamScope& scope, std::string& err) {
    const char* paramName = nullptr;
    if (isParamObject(src, paramName)) {
        auto it = scope.values.find(paramName);
        if (it == scope.values.end()) {
            err = "unknown param: " + std::string(paramName);
            return false;
        }
        dst.set(it->second.as<JsonVariantConst>());
        return true;
    }
    if (src.is<JsonObjectConst>()) {
        JsonObject d = dst.to<JsonObject>();
        for (JsonPairConst kv : src.as<JsonObjectConst>()) {
            if (!resolveInto(kv.value(), d[kv.key()].to<JsonVariant>(), scope, err)) return false;
        }
        return true;
    }
    if (src.is<JsonArrayConst>()) {
        JsonArray d = dst.to<JsonArray>();
        for (JsonVariantConst el : src.as<JsonArrayConst>()) {
            JsonVariant slot = d.add<JsonVariant>();
            if (!resolveInto(el, slot, scope, err)) return false;
        }
        return true;
    }
    if (src.is<const char*>()) {
        const char* s = src.as<const char*>();
        if (strstr(s, "${")) {
            std::string out;
            if (!interpolate(s, scope, out, err)) return false;
            dst.set(out);
        } else {
            dst.set(s);
        }
        return true;
    }
    dst.set(src);
    return true;
}

}  // namespace

ResolveResult resolveParams(JsonVariantConst raw, const ParamScope& scope) {
    ResolveResult r;
    JsonVariant root = r.doc.to<JsonVariant>();
    if (!resolveInto(raw, root, scope, r.error)) {
        r.ok = false;
        r.doc.clear();
    }
    return r;
}

}  // namespace seqb
