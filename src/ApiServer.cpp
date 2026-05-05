#include "ApiServer.h"
#include "core/SequenceCodec.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace seqb {

namespace {
void sendJson(AsyncWebServerRequest* req, const std::string& body, int code = 200) {
    auto* r = req->beginResponse(code, "application/json", body.c_str());
    req->send(r);
}
}  // namespace

ApiServer::ApiServer(
    AsyncWebServer& srv, SequenceStore& s, TriggerStore& t, TriggerManager& tm, ApiHooks hooks)
    : srv_(srv), seqs_(s), trigs_(t), tm_(tm), hooks_(std::move(hooks)) {}

void ApiServer::registerRoutes() {
    srv_.on("/api/schema", HTTP_GET, [this](AsyncWebServerRequest* req) {
        sendJson(req, Registry::instance().dumpSchemaJson());
    });

    srv_.on("/api/sequences", HTTP_GET, [this](AsyncWebServerRequest* req) {
        sendJson(req, SequenceCodec::encodeList(seqs_.all()));
    });

    srv_.on("/api/triggers", HTTP_GET, [this](AsyncWebServerRequest* req) {
        sendJson(req, SequenceCodec::encodeTriggers(trigs_.all()));
    });

    srv_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        sendJson(req, hooks_.currentStatusJson());
    });

    // PUT /api/sequences expects raw body JSON array.
    auto* putSeqs = new AsyncCallbackJsonWebHandler(
        "/api/sequences", [this](AsyncWebServerRequest* req, JsonVariant& json) {
            std::string body;
            serializeJson(json, body);
            auto parsed = SequenceCodec::decodeList(body.c_str());
            seqs_.replaceAll(std::move(parsed));
            seqs_.save();
            sendJson(req, SequenceCodec::encodeList(seqs_.all()));
        });
    putSeqs->setMethod(HTTP_PUT);
    srv_.addHandler(putSeqs);

    auto* putTrigs = new AsyncCallbackJsonWebHandler(
        "/api/triggers", [this](AsyncWebServerRequest* req, JsonVariant& json) {
            std::string body;
            serializeJson(json, body);
            auto parsed = SequenceCodec::decodeTriggers(body.c_str());
            trigs_.replaceAll(std::move(parsed));
            trigs_.save();
            tm_.applyBindings(trigs_.all());
            sendJson(req, SequenceCodec::encodeTriggers(trigs_.all()));
        });
    putTrigs->setMethod(HTTP_PUT);
    srv_.addHandler(putTrigs);

    // Body-less POST (no Content-Type: application/json) — runs with defaults.
    srv_.on("/api/run", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) {
            sendJson(req, R"({"error":"missing id"})", 400);
            return;
        }
        std::string id = req->getParam("id")->value().c_str();
        auto* s = seqs_.findById(id);
        if (!s) {
            sendJson(req, R"({"error":"unknown sequence"})", 404);
            return;
        }
        if (s->broken) {
            sendJson(req, R"({"error":"sequence is broken"})", 422);
            return;
        }
        hooks_.enqueueRun(s->id, JsonVariantConst());
        sendJson(req, R"({"status":"queued"})");
    });

    // POST /api/run?id=ABC — accepts optional JSON body {"args": {...}}.
    auto* runH = new AsyncCallbackJsonWebHandler(
        "/api/run", [this](AsyncWebServerRequest* req, JsonVariant& json) {
            if (!req->hasParam("id")) {
                sendJson(req, R"({"error":"missing id"})", 400);
                return;
            }
            std::string id = req->getParam("id")->value().c_str();
            auto* s = seqs_.findById(id);
            if (!s) {
                sendJson(req, R"({"error":"unknown sequence"})", 404);
                return;
            }
            if (s->broken) {
                sendJson(req, R"({"error":"sequence is broken"})", 422);
                return;
            }
            JsonVariantConst args = json["args"].as<JsonVariantConst>();
            hooks_.enqueueRun(s->id, args);
            sendJson(req, R"({"status":"queued"})");
        });
    runH->setMethod(HTTP_POST);
    srv_.addHandler(runH);
}

}  // namespace seqb
