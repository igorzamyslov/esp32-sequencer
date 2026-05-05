#include "SequenceCodec.h"
#include "Registry.h"
#include <ArduinoJson.h>

namespace seqb {

namespace {

void copyJson(JsonDocument& dst, JsonVariantConst src) {
    dst.clear();
    dst.set(src);
}

FieldType parseFieldType(const char* s) {
    if (!s) return FieldType::String;
    std::string t = s;
    if (t == "bool") return FieldType::Bool;
    if (t == "int") return FieldType::Int;
    if (t == "string") return FieldType::String;
    if (t == "stringlist") return FieldType::StringList;
    if (t == "mac") return FieldType::MacAddress;
    if (t == "enum") return FieldType::Enum;
    if (t == "predicate") return FieldType::PredicateRef;
    return FieldType::String;
}

const char* fieldTypeName(FieldType t) {
    switch (t) {
        case FieldType::Bool:
            return "bool";
        case FieldType::Int:
            return "int";
        case FieldType::String:
            return "string";
        case FieldType::StringList:
            return "stringlist";
        case FieldType::MacAddress:
            return "mac";
        case FieldType::Enum:
            return "enum";
        case FieldType::PredicateRef:
            return "predicate";
    }
    return "string";
}

void decodeNode(JsonVariantConst src, Node& dst, std::string& brokenReason) {
    dst.type = src["type"].as<const char*>() ? src["type"].as<const char*>() : "";
    if (src["params"].is<JsonVariantConst>()) copyJson(dst.params, src["params"]);
    auto childrenObj = src["children"].as<JsonObjectConst>();
    for (JsonPairConst kv : childrenObj) {
        auto& slot = dst.children[std::string(kv.key().c_str())];
        for (JsonVariantConst c : kv.value().as<JsonArrayConst>()) {
            Node child;
            decodeNode(c, child, brokenReason);
            slot.push_back(std::move(child));
        }
    }
    // mark broken if leaf type is unknown (control-flow types are always known)
    if (dst.type != "if" && dst.type != "repeat") {
        if (!Registry::instance().resolveBlock(dst.type) && brokenReason.empty()) {
            brokenReason = "unknown block type: " + dst.type;
        }
    }
}

void encodeNode(JsonObject dst, const Node& src) {
    dst["type"] = src.type;
    // Always emit an object for "params" — never null — so consumers don't
    // have to defend against `null.field` on nodes that left params unset.
    if (src.params.isNull()) {
        dst["params"].to<JsonObject>();
    } else {
        dst["params"].set(src.params.as<JsonVariantConst>());
    }
    JsonObject ch = dst["children"].to<JsonObject>();
    for (const auto& kv : src.children) {
        JsonArray arr = ch[kv.first].to<JsonArray>();
        for (const auto& child : kv.second) {
            JsonObject co = arr.add<JsonObject>();
            encodeNode(co, child);
        }
    }
}

}  // namespace

std::vector<Sequence> SequenceCodec::decodeList(const char* json) {
    std::vector<Sequence> out;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return out;
    for (JsonVariantConst sv : doc.as<JsonArrayConst>()) {
        Sequence s;
        s.id = sv["id"].as<const char*>() ? sv["id"].as<const char*>() : "";
        s.name = sv["name"].as<const char*>() ? sv["name"].as<const char*>() : "";
        s.cooldownMs = sv["cooldownMs"] | 60000U;
        for (JsonVariantConst pv : sv["params"].as<JsonArrayConst>()) {
            ParamDef p;
            p.key = pv["key"].as<const char*>() ? pv["key"].as<const char*>() : "";
            p.type = parseFieldType(pv["type"].as<const char*>());
            p.label = pv["label"].as<const char*>() ? pv["label"].as<const char*>() : "";
            if (pv["enumValues"].is<const char*>())
                p.enumValues = pv["enumValues"].as<const char*>();
            p.required = pv["required"].as<bool>();
            if (!pv["default"].isNull()) p.defaultValue.set(pv["default"]);
            s.params.push_back(std::move(p));
        }
        std::string brokenReason;
        for (JsonVariantConst nv : sv["nodes"].as<JsonArrayConst>()) {
            Node n;
            decodeNode(nv, n, brokenReason);
            s.nodes.push_back(std::move(n));
        }
        if (!brokenReason.empty()) {
            s.broken = true;
            s.brokenReason = brokenReason;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string SequenceCodec::encodeList(const std::vector<Sequence>& seqs) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& s : seqs) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = s.id;
        o["name"] = s.name;
        o["cooldownMs"] = s.cooldownMs;
        JsonArray params = o["params"].to<JsonArray>();
        for (const auto& p : s.params) {
            JsonObject po = params.add<JsonObject>();
            po["key"] = p.key;
            po["type"] = fieldTypeName(p.type);
            po["label"] = p.label;
            if (!p.enumValues.empty()) po["enumValues"] = p.enumValues;
            po["required"] = p.required;
            if (!p.defaultValue.isNull()) po["default"].set(p.defaultValue.as<JsonVariantConst>());
        }
        JsonArray nodes = o["nodes"].to<JsonArray>();
        for (const auto& n : s.nodes) {
            JsonObject no = nodes.add<JsonObject>();
            encodeNode(no, n);
        }
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::vector<TriggerBinding> SequenceCodec::decodeTriggers(const char* json) {
    std::vector<TriggerBinding> out;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return out;
    for (JsonVariantConst tv : doc.as<JsonArrayConst>()) {
        TriggerBinding b;
        b.id = tv["id"].as<const char*>() ? tv["id"].as<const char*>() : "";
        b.type = tv["type"].as<const char*>() ? tv["type"].as<const char*>() : "";
        b.sequenceId = tv["sequenceId"].as<const char*>() ? tv["sequenceId"].as<const char*>() : "";
        b.enabled = tv["enabled"] | true;
        if (tv["params"].is<JsonVariantConst>()) copyJson(b.params, tv["params"]);
        if (!tv["args"].isNull()) copyJson(b.args, tv["args"]);
        out.push_back(std::move(b));
    }
    return out;
}

std::string SequenceCodec::encodeTriggers(const std::vector<TriggerBinding>& ts) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& t : ts) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = t.id;
        o["type"] = t.type;
        o["sequenceId"] = t.sequenceId;
        o["enabled"] = t.enabled;
        o["params"].set(t.params.as<JsonVariantConst>());
        if (!t.args.isNull())
            o["args"].set(t.args.as<JsonVariantConst>());
        else
            o["args"].to<JsonObject>();
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace seqb
