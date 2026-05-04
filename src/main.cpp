#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <string>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "TvController.h"
#include "Sequence.h"        // legacy — Phase 2 deletes it
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

// (Original MENU_HTML retained — replaced in Phase 1B)
static const char MENU_HTML[] = R"HTML(<!doctype html><meta charset=utf-8>
<title>esp32-tv</title>
<style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}
button,a.btn{display:block;width:100%;padding:1em;margin:.5em 0;font-size:1em;text-align:center;text-decoration:none;border:1px solid #888;background:#f4f4f4;color:#000}
.danger{background:#fdd}#status{margin-top:1em;color:#444;min-height:1.2em}</style>
<h1>esp32-tv</h1>
<a class=btn href="/api/sequences">Sequences (JSON)</a>
<a class=btn href="/settings">Settings</a>
<button onclick="post('/trigger')">Trigger default sequence</button>
<button class=danger onclick="if(confirm('Setup mode?'))post('/setup')">Setup mode</button>
<button class=danger onclick="if(confirm('Wipe config?'))post('/reset')">Wipe config</button>
<div id=status></div>
<script>
async function post(p){const s=document.getElementById('status');s.textContent=p+' ...';
  try{const r=await fetch(p,{method:'POST'});s.textContent=p+' → '+r.status+' '+(await r.text());}
  catch(e){s.textContent=p+' failed: '+e;}}
</script>)HTML";

constexpr int LED_PIN = 8;
constexpr uint32_t WIFI_RETRY_BACKOFF_MS = 15000;
constexpr int WIFI_FAILURES_BEFORE_SETUP = 40;

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;
AsyncWebServer http(80);

seqb::NvsPersistence persistence;
seqb::SequenceStore* seqStore   = nullptr;
seqb::TriggerStore*  trigStore  = nullptr;
seqb::Interpreter*   interp     = nullptr;
seqb::TriggerManager* trigMgr   = nullptr;
seqb::ApiServer*     apiServer  = nullptr;

SetupServer* setup_srv = nullptr;

bool in_runtime_ = false;
bool wifi_ready_ = false;
bool runtime_http_started_ = false;
bool ble_started_ = false;
int  wifi_failures_ = 0;
uint32_t next_wifi_retry_ms_ = 0;

// One-deep queue protected by a flag. The pending id is non-volatile because
// access is always inside loop()/handler call-sites, both single-thread on Arduino-ESP32.
volatile bool sequence_pending_ = false;
String pending_seq_id_;

std::string lastError_;
uint32_t    lastRunMs_ = 0;
bool        running_   = false;

void enqueueRun(const std::string& id) {
    if (sequence_pending_) return;  // 1-deep queue
    pending_seq_id_ = id.c_str();
    sequence_pending_ = true;
}

std::string statusJson() {
    JsonDocument d;
    d["running"]    = running_;
    d["lastRunMs"]  = lastRunMs_;
    d["lastError"]  = lastError_;
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
    auto def = seqb::buildDefaults(
        std::string(cfg.pcMac.c_str()),
        std::string(cfg.tvMac.c_str()),
        std::string(cfg.dualsenseMac.c_str()));
    if (seqStore->all().empty())  { seqStore->replaceAll(def.sequences);   seqStore->save(); }
    if (trigStore->all().empty()) { trigStore->replaceAll(def.triggers);   trigStore->save(); }
}

void startRuntimeHttp() {
    if (runtime_http_started_) return;
    Serial.println("[runtime] registering routes");

    http.on("/reset",   HTTP_POST, [](AsyncWebServerRequest* req){
        Config::clear();
        req->send(200, "text/plain", "cleared — rebooting");
        delay(500); ESP.restart();
    });
    http.on("/setup",   HTTP_POST, [](AsyncWebServerRequest* req){
        Config::requestSetupOnNextBoot();
        req->send(200, "text/plain", "entering setup mode — rebooting");
        delay(500); ESP.restart();
    });
    http.on("/",        HTTP_GET,  [](AsyncWebServerRequest* req){
        req->send(200, "text/html", MENU_HTML);
    });
    http.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send(200, "text/html", ConfigForm::renderHtml(cfg, false));
    });
    http.on("/save",    HTTP_POST, [](AsyncWebServerRequest* req){
        ConfigForm::applySave(req, cfg);
        req->send(200, "text/plain", "saved — rebooting in 2s");
        delay(2000); ESP.restart();
    });

    // tv-key kept until Phase 2 (legacy diagnostic): synchronously runs an ad-hoc
    // samsung-keys sequence with the supplied comma-separated keys.
    http.on("/tv-key", HTTP_POST, [](AsyncWebServerRequest* req){
        if (!req->hasParam("k")) { req->send(400, "text/plain", "missing k"); return; }
        seqb::Sequence ad;
        ad.id = "tv-key-adhoc";
        seqb::Node n; n.type = "samsung-keys";
        n.params["keys"] = req->getParam("k")->value().c_str();
        n.params["settle_ms"] = 250;
        n.params["connect_timeout_ms"] = 10000;
        ad.nodes.push_back(n);
        seqb::RunCtx ctx;
        ctx.config = &cfg; ctx.net = &net; ctx.led = &led;
        running_ = true; lastError_ = "";
        auto r = interp->runSequence(ad, ctx);
        running_ = false; lastRunMs_ = millis();
        if (r.status == seqb::RunStatus::Failed) lastError_ = r.error;
        req->send(200, "text/plain",
            r.status == seqb::RunStatus::Ok ? "ok" : r.error.c_str());
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
    Serial.println("[runtime] starting BLE scanner");
    scanner.begin();
    scanner.onHit([](const BleHit& h){
        std::string mac = std::string(h.mac.c_str());
        seqb::BleMacTrigger::instance().onHit(mac, h.rssi);
    });
    scanner.start(0);
    ble_started_ = true;
}

void tryConnectIdle() {
    if (millis() < next_wifi_retry_ms_) return;
    if (net.connectFritzbox()) {
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
    net.configure(cfg.tplinkSsid, cfg.tplinkPass, cfg.fritzboxSsid, cfg.fritzboxPass);
    net.configureTplinkStatic(cfg.tplinkStaticIp, cfg.tplinkGateway);

    seqStore  = new seqb::SequenceStore(persistence);
    trigStore = new seqb::TriggerStore(persistence);
    interp    = new seqb::Interpreter(seqb::Registry::instance());
    trigMgr   = new seqb::TriggerManager(seqb::Registry::instance(),
                                          [](const std::string& sid){ enqueueRun(sid); });
    seqStore->load();
    trigStore->load();
    seedDefaultsIfEmpty();

    static seqb::ApiHooks hooks{
        [](const std::string& sid){ enqueueRun(sid); },
        statusJson,
    };
    apiServer = new seqb::ApiServer(http, *seqStore, *trigStore, *trigMgr, hooks);

    in_runtime_ = true;
    next_wifi_retry_ms_ = 0;
}

void setup() {
    Serial.begin(115200); delay(1500);
    Serial.println("[boot] hi");
    led.attachPin(LED_PIN);
    cfg = Config::load();
    if (!cfg.hasAll()) enterSetupMode();
    else               enterRuntimeMode();
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
            if (!seq) { lastError_ = "unknown sequence: " + sid; }
            else {
                if (ble_started_ && trigMgr->anyPausesBleScan()) scanner.stop();
                led.setState(LedState::Running);
                running_ = true; lastError_ = "";
                seqb::RunCtx ctx;
                ctx.config = &cfg; ctx.net = &net; ctx.led = &led;
                auto r = interp->runSequence(*seq, ctx);
                running_ = false; lastRunMs_ = millis();
                if (r.status == seqb::RunStatus::Failed) {
                    lastError_ = r.error;
                    led.setState(LedState::Error);
                } else {
                    led.setState(LedState::Success);
                }
                if (ble_started_ && trigMgr->anyPausesBleScan()) scanner.start(0);
                if (!net.isConnected() || net.current() != WifiTarget::Fritzbox) {
                    wifi_ready_ = false;
                    next_wifi_retry_ms_ = millis() + 1000;
                }
            }
        }
    }
    delay(10);
}
