#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "TvController.h"
#include "Sequence.h"
#include "SetupServer.h"
#include "ConfigForm.h"
#include "TvController.h"

static const char MENU_HTML[] = R"HTML(
<!doctype html><meta charset=utf-8><title>esp32-tv</title>
<style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}
button,a.btn{display:block;width:100%;padding:1em;margin:.5em 0;font-size:1em;text-align:center;text-decoration:none;border:1px solid #888;background:#f4f4f4;color:#000;cursor:pointer;box-sizing:border-box}
.danger{background:#fdd}
#status{margin-top:1em;color:#444;min-height:1.2em}</style>
<h1>esp32-tv</h1>
<button onclick="post('/trigger')">Trigger sequence (wake PC + TV)</button>
<a class=btn href="/settings">Settings</a>
<button class=danger onclick="if(confirm('Reboot into setup AP mode?'))post('/setup')">Enter setup mode</button>
<button class=danger onclick="if(confirm('Wipe all config and reboot?'))post('/reset')">Wipe config</button>
<div id=status></div>
<script>
async function post(p){
  const s=document.getElementById('status');
  s.textContent=p+' ...';
  try{
    const r=await fetch(p,{method:'POST'});
    s.textContent=p+' → '+r.status+' '+(await r.text());
  }catch(e){ s.textContent=p+' failed: '+e; }
}
</script>
)HTML";

constexpr int LED_PIN = 8;
constexpr uint32_t COOLDOWN_MS = 60000;
constexpr uint32_t WIFI_RETRY_BACKOFF_MS = 15000;
constexpr int WIFI_FAILURES_BEFORE_SETUP = 40; // ~10 min of failed retries (only if cfg.setupFallback)

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;
AsyncWebServer runtime_http(80);

// Setup-mode owner; allocated only if config missing.
SetupServer* setup_srv = nullptr;

// Runtime-mode state
bool in_runtime_ = false;
bool wifi_ready_ = false;
bool runtime_http_started_ = false;
bool ble_started_ = false;
int wifi_failures_ = 0;
uint32_t next_wifi_retry_ms_ = 0;
uint32_t cooldown_until_ = 0;
volatile bool sequence_pending_ = false;
volatile bool tv_key_pending_ = false;
String tv_key_value_;

void enterSetupMode() {
  Serial.println("[boot] entering setup mode");
  led.setState(LedState::Setup);
  setup_srv = new SetupServer(cfg, scanner);
  setup_srv->begin();
}

void startRuntimeHttp() {
  if (runtime_http_started_) return;
  Serial.println("[runtime] registering HTTP routes");
  runtime_http.on("/trigger", HTTP_POST, [](AsyncWebServerRequest* req){
    sequence_pending_ = true;
    cooldown_until_ = millis() + COOLDOWN_MS;
    req->send(200, "text/plain", "queued");
  });
  runtime_http.on("/reset", HTTP_POST, [](AsyncWebServerRequest* req){
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(500);
    ESP.restart();
  });
  runtime_http.on("/setup", HTTP_POST, [](AsyncWebServerRequest* req){
    Config::requestSetupOnNextBoot();
    req->send(200, "text/plain", "entering setup mode — rebooting");
    delay(500);
    ESP.restart();
  });
  runtime_http.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", MENU_HTML);
  });
  runtime_http.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", ConfigForm::renderHtml(cfg, false));
  });
  runtime_http.on("/save", HTTP_POST, [](AsyncWebServerRequest* req){
    ConfigForm::applySave(req, cfg);
    req->send(200, "text/plain", "saved — rebooting in 2s");
    delay(2000);
    ESP.restart();
  });
  // Diagnostic: send a single Tizen key over the WS without running the full
  // sequence. Use POST /tv-key?k=KEY_HDMI3 — handy for figuring out which key
  // a particular Tizen model accepts for input switching.
  runtime_http.on("/tv-key", HTTP_POST, [](AsyncWebServerRequest* req){
    if (!req->hasParam("k")) { req->send(400, "text/plain", "missing k"); return; }
    if (tv_key_pending_) { req->send(429, "text/plain", "busy"); return; }
    tv_key_value_ = req->getParam("k")->value();
    tv_key_pending_ = true;
    req->send(200, "text/plain", String("queued: ") + tv_key_value_);
  });
  runtime_http.begin();
  runtime_http_started_ = true;
  Serial.printf("[runtime] http on http://%s/\n", net.localIp().c_str());
}

void startBle() {
  if (ble_started_) return;
  Serial.println("[runtime] starting BLE scanner");
  scanner.begin();
  scanner.onHit([](const BleHit& h){
    if (millis() < cooldown_until_) return;
    String mac = h.mac; mac.toLowerCase();
    String wanted = cfg.dualsenseMac; wanted.toLowerCase();
    if (mac != wanted) return;
    sequence_pending_ = true;
    cooldown_until_ = millis() + COOLDOWN_MS;
    Serial.printf("[runtime] DualSense detected rssi=%d, queuing sequence\n", h.rssi);
  });
  scanner.start(0);
  ble_started_ = true;
  Serial.println("[runtime] BLE scanner running");
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
      Serial.printf("[runtime] %d failed wifi attempts — falling back to setup mode\n",
                    wifi_failures_);
      net.disconnect();
      in_runtime_ = false;
      enterSetupMode();
      return;
    }
    led.setState(LedState::Error);
    Serial.printf("[runtime] idle wifi unreachable (attempt %d), retry in %lus\n",
                  wifi_failures_, (unsigned long)(WIFI_RETRY_BACKOFF_MS / 1000));
    next_wifi_retry_ms_ = millis() + WIFI_RETRY_BACKOFF_MS;
  }
}

void enterRuntimeMode() {
  Serial.println("[boot] entering runtime mode");
  net.configure(cfg.tplinkSsid, cfg.tplinkPass, cfg.fritzboxSsid, cfg.fritzboxPass);
  net.configureTplinkStatic(cfg.tplinkStaticIp, cfg.tplinkGateway);
  // Idle on Fritzbox: that's where phones live and where /trigger requests come from.
  // The sequence hops to TP-Link briefly to WoL the PC, then comes back.
  // Connect attempt is driven by the loop with backoff so a missing AP doesn't brick boot.
  in_runtime_ = true;
  next_wifi_retry_ms_ = 0; // try immediately on first loop tick
}

void setup() {
  Serial.begin(115200);
  delay(1500); // wait for USB-CDC to enumerate before first println
  Serial.println("[boot] hi");
  led.attachPin(LED_PIN);

  cfg = Config::load();
  if (!cfg.hasAll()) {
    enterSetupMode();
  } else {
    enterRuntimeMode();
  }
}

void loop() {
  led.tick(millis());

  if (setup_srv) setup_srv->loop();

  if (in_runtime_) {
    if (!wifi_ready_) {
      tryConnectIdle();
    } else if (!net.isConnected()) {
      Serial.println("[runtime] wifi dropped, will reconnect");
      led.setState(LedState::Error);
      wifi_ready_ = false;
      next_wifi_retry_ms_ = millis() + 1000;
    } else if (tv_key_pending_) {
      tv_key_pending_ = false;
      String keys = tv_key_value_;
      if (ble_started_) scanner.stop();
      if (net.current() != WifiTarget::Fritzbox) net.hopTo(WifiTarget::Fritzbox);
      TvController tv;
      bool tokenChanged = false;
      String newToken;
      tv.configure(cfg.tvIp, cfg.tvToken,
                   [&](const String& t){ tokenChanged = true; newToken = t; });
      if (tv.connectWithRetry(10000)) {
        tv.pump(200);
        // keys is a comma-separated list, e.g. "KEY_SOURCE,KEY_LEFT,KEY_RIGHT,KEY_ENTER"
        int from = 0;
        while (from <= (int)keys.length()) {
          int comma = keys.indexOf(',', from);
          String one = comma < 0 ? keys.substring(from) : keys.substring(from, comma);
          one.trim();
          if (one.length()) {
            tv.sendKey(one.c_str());
            tv.pump(250);
          }
          if (comma < 0) break;
          from = comma + 1;
        }
        tv.pump(300);
        tv.disconnect();
      } else {
        Serial.println("[tv-key] tv ws failed");
      }
      if (tokenChanged) { cfg.tvToken = newToken; cfg.save(); }
      if (ble_started_) scanner.start(0);
    } else if (sequence_pending_) {
      sequence_pending_ = false;
      if (ble_started_) scanner.stop();
      Sequence seq({ &cfg, &net, &led });
      seq.run();
      if (ble_started_) scanner.start(0);
      // After sequence: if we're not back on the idle network, force a reconnect cycle.
      // Sequence::run handles its own LED state on failure; the watchdog above heals WiFi.
      if (!net.isConnected() || net.current() != WifiTarget::Fritzbox) {
        Serial.println("[runtime] post-sequence: not on idle wifi, reconnecting");
        wifi_ready_ = false;
        next_wifi_retry_ms_ = millis() + 1000;
      }
    }
  }

  delay(10);
}
