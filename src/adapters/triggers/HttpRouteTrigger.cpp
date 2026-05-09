#include "HttpRouteTrigger.h"
#include "QueryCoerce.h"
#include "core/Registry.h"
#include "core/Sequence.h"
#include <ESPAsyncWebServer.h>
#include <map>
#include <string>
#include <vector>

using namespace seqb;

namespace {

AsyncWebServer* g_server = nullptr;

constexpr FieldDef FIELDS[] = {
    {"path", FieldType::String, "HTTP path (e.g. /play)", "/play", nullptr, true},
};
constexpr TriggerSchema SCH = {"http-route", "HTTP route", FIELDS, 1, true};

struct Active {
    std::string path;
    std::string sequenceId;
    Trigger::FireCallback cb;
    Trigger::SequenceLookup lookup;
    JsonDocument defaultArgs;
};

std::map<std::string, Active> g_active;

// Look up by id at fire time so we never hold a dangling reference into
// g_active. ESPAsyncWebServer can't remove path handlers, so old handlers
// from previous applyBindings cycles stay registered; capturing by reference
// dereferences memory that unbind() has destroyed.
static void registerHandler(const std::string& path, const std::string& id) {
    if (!g_server) return;
    g_server->on(path.c_str(), HTTP_POST, [id](AsyncWebServerRequest* req) {
        auto it = g_active.find(id);
        if (it == g_active.end()) {
            req->send(410, "application/json", R"({"error":"binding gone"})");
            return;
        }
        Active& a = it->second;
        const Sequence* seq = a.lookup ? a.lookup(a.sequenceId) : nullptr;
        std::vector<ParamDef> empty;
        const auto& pdefs = seq ? seq->params : empty;

        std::map<std::string, std::string> q;
        for (size_t i = 0; i < req->params(); ++i) {
            auto* p = req->getParam(i);
            q.emplace(p->name().c_str(), p->value().c_str());
        }

        auto cr = coerceQueryArgs(a.defaultArgs.as<JsonVariantConst>(), pdefs, q);
        if (!cr.ok) {
            std::string body = std::string("{\"error\":\"") + cr.error + "\"}";
            req->send(400, "application/json", body.c_str());
            return;
        }
        a.cb(a.sequenceId, cr.doc.as<JsonVariantConst>());
        req->send(200, "text/plain", "queued");
    });
}

}  // namespace

namespace seqb {

void HttpRouteTrigger::setServer(AsyncWebServer* s) {
    g_server = s;
}

HttpRouteTrigger& HttpRouteTrigger::instance() {
    static HttpRouteTrigger i;
    return i;
}

const TriggerSchema& HttpRouteTrigger::schema() const {
    return SCH;
}

void HttpRouteTrigger::bind(const std::string& id,
                            JsonVariantConst params,
                            const std::string& seq,
                            JsonVariantConst defaultArgs,
                            SequenceLookup lookup,
                            FireCallback cb) {
    const char* path = params["path"].as<const char*>();
    if (!path || !path[0]) return;
    Active a;
    a.path = path;
    a.sequenceId = seq;
    a.cb = cb;
    a.lookup = lookup;
    if (!defaultArgs.isNull()) a.defaultArgs.set(defaultArgs);
    std::string path_str = a.path;
    g_active[id] = std::move(a);

    registerHandler(path_str, id);
}

void HttpRouteTrigger::unbind(const std::string& id) {
    auto it = g_active.find(id);
    if (it == g_active.end()) return;
    // ESPAsyncWebServer has no remove-by-path. Re-binding the same path
    // works because handler insertion order favors latest entries.
    // Documented limitation: removing a path requires reboot.
    g_active.erase(it);
}

namespace {
struct _Reg {
    _Reg() { Registry::instance().registerTrigger(&HttpRouteTrigger::instance()); }
} _reg;
}  // namespace

}  // namespace seqb
