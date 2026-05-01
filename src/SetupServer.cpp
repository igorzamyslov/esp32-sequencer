#include "SetupServer.h"
#include <WiFi.h>
#include <ArduinoJson.h>

namespace {
    const char* INDEX_HTML = R"HTML(
<!doctype html><meta charset=utf-8><title>esp32-tv setup</title>
<style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}
input{width:100%;padding:.4em;margin:.2em 0;box-sizing:border-box}
button{padding:.5em 1em;margin:.4em 0}
fieldset{margin:1em 0}</style>
<h1>esp32-tv setup</h1>
<form method=post action=/save>
<fieldset><legend>WiFi: TP-Link (where the PC is reachable for WoL)</legend>
SSID <input name=tpSsid>
Password <input name=tpPass type=password>
</fieldset>
<fieldset><legend>WiFi: Fritzbox (where the TV is)</legend>
SSID <input name=fbSsid>
Password <input name=fbPass type=password>
</fieldset>
<fieldset><legend>Devices</legend>
PC's wired-NIC MAC <input name=pcMac placeholder="AA:BB:CC:DD:EE:FF">
Samsung TV IP <input name=tvIp placeholder="192.168.178.42">
Samsung TV MAC (WiFi) <input name=tvMac placeholder="AA:BB:CC:DD:EE:FF">
DualSense MAC <input name=dsMac id=dsMac placeholder="(use Pair button below)">
<button type=button onclick="startPair()">Pair gamepad</button>
<span id=pairStatus></span>
</fieldset>
<button type=submit>Save and reboot</button>
</form>
<script>
async function startPair(){
  document.getElementById('pairStatus').textContent='scanning 30s — power on the controller';
  await fetch('/pair-start', {method:'POST'});
  let started = Date.now();
  let timer = setInterval(async ()=>{
    let r = await fetch('/pair-status'); let j = await r.json();
    if (j.mac){
      document.getElementById('dsMac').value = j.mac;
      document.getElementById('pairStatus').textContent = 'paired (RSSI '+j.rssi+')';
      clearInterval(timer);
    } else if (Date.now() - started > 30000){
      document.getElementById('pairStatus').textContent = 'no controller seen';
      clearInterval(timer);
    }
  }, 1000);
}
</script>
)HTML";
}

void SetupServer::begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("esp32-tv-setup");
    Serial.printf("[setup] AP up at %s\n", WiFi.softAPIP().toString().c_str());

    server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* r){ handleRoot(r); });
    server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r){ handleSave(r); });
    server_.on("/pair-start", HTTP_POST, [this](AsyncWebServerRequest* r){ handlePairStart(r); });
    server_.on("/pair-status", HTTP_GET, [this](AsyncWebServerRequest* r){ handlePairStatus(r); });
    server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest* r){ handleReset(r); });
    server_.begin();

    scanner_.begin();
    scanner_.onHit([this](const BleHit& h){
        if (!pairing_) return;
        if (millis() - pair_started_at_ > 30000) { pairing_ = false; scanner_.stop(); return; }
        if (h.rssi > best_ds_rssi_) {
            best_ds_rssi_ = h.rssi;
            best_ds_mac_ = h.mac;
        }
    });
}

void SetupServer::handleRoot(AsyncWebServerRequest* req) {
    req->send(200, "text/html", INDEX_HTML);
}

static String arg(AsyncWebServerRequest* req, const char* name) {
    if (!req->hasParam(name, true)) return "";
    return req->getParam(name, true)->value();
}

void SetupServer::handleSave(AsyncWebServerRequest* req) {
    cfg_.tplinkSsid    = arg(req, "tpSsid");
    cfg_.tplinkPass    = arg(req, "tpPass");
    cfg_.fritzboxSsid  = arg(req, "fbSsid");
    cfg_.fritzboxPass  = arg(req, "fbPass");
    cfg_.pcMac         = arg(req, "pcMac");
    cfg_.tvIp          = arg(req, "tvIp");
    cfg_.tvMac         = arg(req, "tvMac");
    cfg_.dualsenseMac  = arg(req, "dsMac");
    // tvToken is left as-is; populated on first sequence run.
    cfg_.save();
    req->send(200, "text/plain", "saved — rebooting in 2s");
    delay(2000);
    ESP.restart();
}

void SetupServer::handlePairStart(AsyncWebServerRequest* req) {
    best_ds_mac_ = "";
    best_ds_rssi_ = -127;
    pairing_ = true;
    pair_started_at_ = millis();
    scanner_.start(30000);
    req->send(200, "text/plain", "scanning");
}

void SetupServer::handlePairStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["mac"] = best_ds_mac_;
    doc["rssi"] = best_ds_rssi_;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void SetupServer::handleReset(AsyncWebServerRequest* req) {
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(1000);
    ESP.restart();
}
