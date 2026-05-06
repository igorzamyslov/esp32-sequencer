#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <string>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "SetupServer.h"
#include "ConfigForm.h"
#include "ApiServer.h"
#include "core/Registry.h"
#include "core/SequenceStore.h"
#include "core/TriggerStore.h"
#include "core/Interpreter.h"
#include "core/TriggerManager.h"
#include "core/DefaultSequences.h"
#include "adapters/persistence/NvsPersistence.h"
#include "adapters/triggers/HttpRouteTrigger.h"
#include "adapters/triggers/BleMacTrigger.h"
#include "web/web_assets.h"

constexpr int LED_PIN = 8;
constexpr uint32_t WIFI_RETRY_BACKOFF_MS = 15000;
constexpr int WIFI_FAILURES_BEFORE_SETUP = 40;

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;
AsyncWebServer http(80);

seqb::NvsPersistence persistence;
seqb::SequenceStore* seqStore = nullptr;
seqb::TriggerStore* trigStore = nullptr;
seqb::Interpreter* interp = nullptr;
seqb::TriggerManager* trigMgr = nullptr;
seqb::ApiServer* apiServer = nullptr;

SetupServer* setup_srv = nullptr;

bool in_runtime_ = false;
bool wifi_ready_ = false;
bool runtime_http_started_ = false;
bool ble_started_ = false;
int wifi_failures_ = 0;
uint32_t next_wifi_retry_ms_ = 0;

// One-deep queue protected by a flag. The pending id is non-volatile because
// access is always inside loop()/handler call-sites, both single-thread on Arduino-ESP32.
volatile bool sequence_pending_ = false;
String pending_seq_id_;
JsonDocument pending_args_;  // owned

std::string lastError_;
uint32_t lastRunMs_ = 0;
bool running_ = false;

void enqueueRun(const std::string& id, JsonVariantConst args = JsonVariantConst()) {
    if (sequence_pending_) return;  // 1-deep queue
    pending_seq_id_ = id.c_str();
    pending_args_.clear();
    if (!args.isNull()) pending_args_.set(args);
    sequence_pending_ = true;
}

std::string statusJson() {
    JsonDocument d;
    d["running"] = running_;
    d["lastRunMs"] = lastRunMs_;
    d["lastError"] = lastError_;
    std::string s;
    serializeJson(d, s);
    return s;
}

void enterSetupMode() {
    Serial.println("[boot] entering setup mode");
    led.setState(LedState::Setup);
    setup_srv = new SetupServer(cfg, scanner);
    setup_srv->begin();
}

void seedDefaultsIfEmpty() {
    if (!seqStore->all().empty() && !trigStore->all().empty()) return;
    Serial.println("[boot] seeding defaults");
    auto def = seqb::buildDefaults();
    if (seqStore->all().empty()) {
        seqStore->replaceAll(def.sequences);
        seqStore->save();
    }
    if (trigStore->all().empty()) {
        trigStore->replaceAll(def.triggers);
        trigStore->save();
    }
}

static void sendGz(AsyncWebServerRequest* req, const unsigned char* data, size_t len) {
    auto* r = req->beginResponse(200, "text/html", data, len);
    r->addHeader("Content-Encoding", "gzip");
    req->send(r);
}

void startRuntimeHttp() {
    if (runtime_http_started_) return;
    Serial.println("[runtime] registering routes");

    http.on("/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        Config::clear();
        req->send(200, "text/plain", "cleared — rebooting");
        delay(500);
        ESP.restart();
    });
    http.on("/setup", HTTP_POST, [](AsyncWebServerRequest* req) {
        Config::requestSetupOnNextBoot();
        req->send(200, "text/plain", "entering setup mode — rebooting");
        delay(500);
        ESP.restart();
    });
    http.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendGz(req, LANDING_HTML_GZ, LANDING_HTML_GZ_LEN);
    });
    http.on("/edit", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendGz(req, EDITOR_HTML_GZ, EDITOR_HTML_GZ_LEN);
    });
    http.on("/triggers", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendGz(req, TRIGGERS_HTML_GZ, TRIGGERS_HTML_GZ_LEN);
    });
    http.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", ConfigForm::renderHtml(cfg, false));
    });
    http.on("/save", HTTP_POST, [](AsyncWebServerRequest* req) {
        ConfigForm::applySave(req, cfg);
        req->send(200, "text/plain", "saved — rebooting in 2s");
        delay(2000);
        ESP.restart();
    });

    // Wire trigger adapter dependencies BEFORE applying bindings.
    seqb::HttpRouteTrigger::setServer(&http);
    seqb::BleMacTrigger::setScanner(&scanner);

    // ApiServer registers /api/* routes.
    apiServer->registerRoutes();

    // Apply bindings (this registers HTTP-route triggers as handlers on `http`).
    trigMgr->applyBindings(trigStore->all());

    http.begin();
    runtime_http_started_ = true;
    Serial.printf("[runtime] http on http://%s/\n", net.localIp().c_str());
}

void startBle() {
    if (ble_started_) return;
    if (seqb::BleMacTrigger::activeCount() == 0) {
        Serial.println("[runtime] no ble-mac triggers — skipping BLE scanner");
        return;
    }
    Serial.println("[runtime] starting BLE scanner");
    scanner.begin();
    scanner.onHit([](const BleHit& h) {
        std::string mac = std::string(h.mac.c_str());
        seqb::BleMacTrigger::instance().onHit(mac, h.rssi);
    });
    scanner.start(0);
    ble_started_ = true;
}

void tryConnectIdle() {
    if (millis() < next_wifi_retry_ms_) return;
    if (net.connectIdle(cfg.idleSsid, cfg.idlePass)) {
        led.setState(LedState::Idle);
        wifi_ready_ = true;
        wifi_failures_ = 0;
        startRuntimeHttp();
        startBle();
    } else {
        wifi_failures_++;
        if (cfg.setupFallback && wifi_failures_ >= WIFI_FAILURES_BEFORE_SETUP) {
            net.disconnect();
            in_runtime_ = false;
            enterSetupMode();
            return;
        }
        led.setState(LedState::Error);
        next_wifi_retry_ms_ = millis() + WIFI_RETRY_BACKOFF_MS;
    }
}

void enterRuntimeMode() {
    Serial.println("[boot] entering runtime mode");

    seqStore = new seqb::SequenceStore(persistence);
    trigStore = new seqb::TriggerStore(persistence);
    interp = new seqb::Interpreter(seqb::Registry::instance());
    trigMgr = new seqb::TriggerManager(
        seqb::Registry::instance(),
        [](const std::string& sid, JsonVariantConst args) { enqueueRun(sid, args); },
        [](const std::string& id) -> const seqb::Sequence* { return seqStore->findById(id); });
    seqStore->load();
    trigStore->load();
    seedDefaultsIfEmpty();

    static seqb::ApiHooks hooks{
        [](const std::string& sid, JsonVariantConst args) { enqueueRun(sid, args); },
        statusJson,
    };
    apiServer = new seqb::ApiServer(http, *seqStore, *trigStore, *trigMgr, hooks);

    in_runtime_ = true;
    next_wifi_retry_ms_ = 0;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[boot] hi");
    led.attachPin(LED_PIN);
    cfg = Config::load();
    if (!cfg.hasAll())
        enterSetupMode();
    else
        enterRuntimeMode();
}

void loop() {
    led.tick(millis());
    if (setup_srv) setup_srv->loop();

    if (in_runtime_) {
        if (!wifi_ready_) {
            tryConnectIdle();
        } else if (!net.isConnected()) {
            wifi_ready_ = false;
            next_wifi_retry_ms_ = millis() + 1000;
            led.setState(LedState::Error);
        } else if (sequence_pending_) {
            sequence_pending_ = false;
            String sidArduino = pending_seq_id_;
            pending_seq_id_ = "";
            std::string sid(sidArduino.c_str());
            auto* seq = seqStore->findById(sid);
            if (!seq) {
                lastError_ = "unknown sequence: " + sid;
            } else {
                if (ble_started_ && trigMgr->anyPausesBleScan()) scanner.stop();
                led.setState(LedState::Running);
                running_ = true;
                lastError_ = "";
                seqb::RunCtx ctx;
                ctx.config = &cfg;
                ctx.net = &net;
                ctx.led = &led;
                auto r = interp->runSequence(*seq, ctx, pending_args_.as<JsonVariantConst>());
                running_ = false;
                lastRunMs_ = millis();
                pending_args_.clear();
                if (r.status == seqb::RunStatus::Failed) {
                    lastError_ = r.error;
                    led.setState(LedState::Error);
                } else {
                    led.setState(LedState::Success);
                }
                if (ble_started_ && trigMgr->anyPausesBleScan()) scanner.start(0);
                if (!net.isConnected() || net.currentSsid() != cfg.idleSsid) {
                    wifi_ready_ = false;
                    next_wifi_retry_ms_ = millis() + 1000;
                }
            }
        }
    }
    delay(10);
}
