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
    // Memory budget on this device is tight; we hold:
    //   - AsyncJson's body buffer  (~bodyLen bytes)
    //   - AsyncJson's parsed doc   (~2x bodyLen with internal nodes)
    //   - the parsed Sequence list (each Node owns its own JsonDocument)
    //   - if we're not careful, the old list AND a re-encoded response too.
    // So: free the old list first, and ack with a tiny body instead of
    // echoing the full list. Body cap raised to 64 KB.
    auto* putSeqs = new AsyncCallbackJsonWebHandler(
        "/api/sequences", [this](AsyncWebServerRequest* req, JsonVariant& json) {
            seqs_.replaceAll({});  // free old in-memory list before parsing new
            auto parsed = SequenceCodec::decodeList(json.as<JsonVariantConst>());
            seqs_.replaceAll(std::move(parsed));
            seqs_.save();
            sendJson(req, R"({"status":"ok"})");
        });
    putSeqs->setMethod(HTTP_PUT);
    putSeqs->setMaxContentLength(64 * 1024);
    srv_.addHandler(putSeqs);

    auto* putTrigs = new AsyncCallbackJsonWebHandler(
        "/api/triggers", [this](AsyncWebServerRequest* req, JsonVariant& json) {
            trigs_.replaceAll({});  // free old before parsing new
            auto parsed = SequenceCodec::decodeTriggers(json.as<JsonVariantConst>());
            trigs_.replaceAll(std::move(parsed));
            trigs_.save();
            tm_.applyBindings(trigs_.all());
            sendJson(req, R"({"status":"ok"})");
        });
    putTrigs->setMethod(HTTP_PUT);
    putTrigs->setMaxContentLength(32 * 1024);
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
