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
};

std::map<std::string, Active> g_active;

void registerHandler(const std::string& path,
                     const std::string& sequenceId,
                     Trigger::FireCallback cb) {
    if (!g_server) return;
    g_server->on(path.c_str(), HTTP_POST, [sequenceId, cb](AsyncWebServerRequest* req) {
        cb(sequenceId);
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
                            FireCallback cb) {
    const char* path = params["path"].as<const char*>();
    if (!path || !path[0]) return;
    g_active[id] = {std::string(path), seq, cb};
    registerHandler(std::string(path), seq, cb);
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
