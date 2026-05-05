#include "HttpRouteTrigger.h"
#include "core/Registry.h"
#include <ESPAsyncWebServer.h>
#include <map>
#include <string>

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

static void registerHandler(Active& a) {
    if (!g_server) return;
    // For now: handler fires with defaultArgs only. Task 6 will add query coercion.
    g_server->on(a.path.c_str(), HTTP_POST, [&a](AsyncWebServerRequest* req) {
        a.cb(a.sequenceId, a.defaultArgs.as<JsonVariantConst>());
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
    g_active[id] = std::move(a);

    // For now: handler fires with defaultArgs only. Task 6 will add query coercion.
    registerHandler(g_active[id]);
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
