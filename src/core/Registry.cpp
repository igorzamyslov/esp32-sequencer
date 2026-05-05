#include "Registry.h"
#include <ArduinoJson.h>

namespace seqb {

static Registry* g_registry = nullptr;

Registry& Registry::instance() {
    if (!g_registry) g_registry = new Registry();
    return *g_registry;
}

void Registry::reset() {
    delete g_registry;
    g_registry = nullptr;
}

void Registry::registerBlock(Block* b) {
    blocks_[b->schema().type] = b;
}
void Registry::registerPredicate(Predicate* p) {
    predicates_[p->schema().type] = p;
}
void Registry::registerTrigger(Trigger* t) {
    triggers_[t->schema().type] = t;
}

Block* Registry::resolveBlock(const std::string& t) const {
    auto it = blocks_.find(t);
    return it == blocks_.end() ? nullptr : it->second;
}
Predicate* Registry::resolvePredicate(const std::string& t) const {
    auto it = predicates_.find(t);
    return it == predicates_.end() ? nullptr : it->second;
}
Trigger* Registry::resolveTrigger(const std::string& t) const {
    auto it = triggers_.find(t);
    return it == triggers_.end() ? nullptr : it->second;
}

static void writeFields(JsonArray dst, const FieldDef* f, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        JsonObject o = dst.add<JsonObject>();
        o["key"] = f[i].key;
        const char* type = "string";
        switch (f[i].type) {
            case FieldType::Bool:
                type = "bool";
                break;
            case FieldType::Int:
                type = "int";
                break;
            case FieldType::String:
                type = "string";
                break;
            case FieldType::StringList:
                type = "stringList";
                break;
            case FieldType::MacAddress:
                type = "mac";
                break;
            case FieldType::Enum:
                type = "enum";
                break;
            case FieldType::PredicateRef:
                type = "predicateRef";
                break;
        }
        o["type"] = type;
        o["label"] = f[i].label;
        if (f[i].defaultValue) o["default"] = f[i].defaultValue;
        if (f[i].enumValues) o["enum"] = f[i].enumValues;
        o["required"] = f[i].required;
    }
}

std::string Registry::dumpSchemaJson() const {
    JsonDocument doc;
    JsonArray blocks = doc["blocks"].to<JsonArray>();
    for (auto& kv : blocks_) {
        const auto& s = kv.second->schema();
        JsonObject o = blocks.add<JsonObject>();
        o["type"] = s.type;
        o["label"] = s.label;
        o["category"] = s.category;
        writeFields(o["fields"].to<JsonArray>(), s.fields, s.fieldCount);
        JsonArray slots = o["childSlots"].to<JsonArray>();
        for (std::size_t i = 0; i < s.childSlotCount; ++i)
            slots.add(s.childSlots[i]);
    }
    JsonArray preds = doc["predicates"].to<JsonArray>();
    for (auto& kv : predicates_) {
        const auto& s = kv.second->schema();
        JsonObject o = preds.add<JsonObject>();
        o["type"] = s.type;
        o["label"] = s.label;
        writeFields(o["fields"].to<JsonArray>(), s.fields, s.fieldCount);
    }
    JsonArray trigs = doc["triggers"].to<JsonArray>();
    for (auto& kv : triggers_) {
        const auto& s = kv.second->schema();
        JsonObject o = trigs.add<JsonObject>();
        o["type"] = s.type;
        o["label"] = s.label;
        o["pausesBleScan"] = s.pausesBleScan;
        writeFields(o["fields"].to<JsonArray>(), s.fields, s.fieldCount);
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace seqb
