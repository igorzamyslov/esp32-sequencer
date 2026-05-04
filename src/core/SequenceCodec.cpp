#include "SequenceCodec.h"
#include "Registry.h"
#include <ArduinoJson.h>

namespace seqb {

namespace {

void copyJson(JsonDocument& dst, JsonVariantConst src) {
    dst.clear();
    dst.set(src);
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
    dst["params"].set(src.params.as<JsonVariantConst>());
    JsonObject ch = dst["children"].to<JsonObject>();
    for (auto& kv : src.children) {
        JsonArray arr = ch[kv.first].to<JsonArray>();
        for (auto& child : kv.second) {
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
        s.id         = sv["id"].as<const char*>() ? sv["id"].as<const char*>() : "";
        s.name       = sv["name"].as<const char*>() ? sv["name"].as<const char*>() : "";
        s.cooldownMs = sv["cooldownMs"] | 60000U;
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
    for (auto& s : seqs) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]         = s.id;
        o["name"]       = s.name;
        o["cooldownMs"] = s.cooldownMs;
        JsonArray nodes = o["nodes"].to<JsonArray>();
        for (auto& n : s.nodes) {
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
        b.id         = tv["id"].as<const char*>()         ? tv["id"].as<const char*>() : "";
        b.type       = tv["type"].as<const char*>()       ? tv["type"].as<const char*>() : "";
        b.sequenceId = tv["sequenceId"].as<const char*>() ? tv["sequenceId"].as<const char*>() : "";
        b.enabled    = tv["enabled"] | true;
        if (tv["params"].is<JsonVariantConst>()) copyJson(b.params, tv["params"]);
        out.push_back(std::move(b));
    }
    return out;
}

std::string SequenceCodec::encodeTriggers(const std::vector<TriggerBinding>& ts) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto& t : ts) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]         = t.id;
        o["type"]       = t.type;
        o["sequenceId"] = t.sequenceId;
        o["enabled"]    = t.enabled;
        o["params"].set(t.params.as<JsonVariantConst>());
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace seqb
