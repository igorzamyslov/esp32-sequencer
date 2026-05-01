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
SSID <input name=tpSsid value="{{tpSsid}}">
Password <input name=tpPass type=password placeholder="{{tpPassHint}}">
Static IP (optional, leave empty for DHCP) <input name=tpIp value="{{tpIp}}" placeholder="192.168.137.253">
Gateway (only if static IP is set) <input name=tpGw value="{{tpGw}}" placeholder="192.168.137.1">
</fieldset>
<fieldset><legend>WiFi: Fritzbox (where the TV is)</legend>
SSID <input name=fbSsid value="{{fbSsid}}">
Password <input name=fbPass type=password placeholder="{{fbPassHint}}">
</fieldset>
<fieldset><legend>Devices</legend>
PC's wired-NIC MAC <input name=pcMac value="{{pcMac}}" placeholder="AA:BB:CC:DD:EE:FF">
Samsung TV IP <input name=tvIp value="{{tvIp}}" placeholder="192.168.178.42">
Samsung TV MAC (WiFi) <input name=tvMac value="{{tvMac}}" placeholder="AA:BB:CC:DD:EE:FF">
DualSense MAC <input name=dsMac id=dsMac value="{{dsMac}}" placeholder="(use Pair button below)">
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
    server_.onNotFound([](AsyncWebServerRequest* r){ r->redirect("http://192.168.4.1/"); });
    server_.begin();

    dns_.start(53, "*", WiFi.softAPIP());

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

void SetupServer::loop() {
    dns_.processNextRequest();
}

static String htmlAttrEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

void SetupServer::handleRoot(AsyncWebServerRequest* req) {
    String html = INDEX_HTML;
    html.replace("{{tpSsid}}", htmlAttrEscape(cfg_.tplinkSsid));
    html.replace("{{tpIp}}",   htmlAttrEscape(cfg_.tplinkStaticIp));
    html.replace("{{tpGw}}",   htmlAttrEscape(cfg_.tplinkGateway));
    html.replace("{{fbSsid}}", htmlAttrEscape(cfg_.fritzboxSsid));
    html.replace("{{pcMac}}",  htmlAttrEscape(cfg_.pcMac));
    html.replace("{{tvIp}}",   htmlAttrEscape(cfg_.tvIp));
    html.replace("{{tvMac}}",  htmlAttrEscape(cfg_.tvMac));
    html.replace("{{dsMac}}",  htmlAttrEscape(cfg_.dualsenseMac));
    html.replace("{{tpPassHint}}", cfg_.tplinkPass.length()   ? "(saved — leave empty to keep)" : "");
    html.replace("{{fbPassHint}}", cfg_.fritzboxPass.length() ? "(saved — leave empty to keep)" : "");
    req->send(200, "text/html", html);
}

static String arg(AsyncWebServerRequest* req, const char* name) {
    if (!req->hasParam(name, true)) return "";
    return req->getParam(name, true)->value();
}

void SetupServer::handleSave(AsyncWebServerRequest* req) {
    cfg_.tplinkSsid    = arg(req, "tpSsid");
    String tpPass      = arg(req, "tpPass");
    if (tpPass.length()) cfg_.tplinkPass = tpPass; // empty = keep existing
    cfg_.tplinkStaticIp= arg(req, "tpIp");
    cfg_.tplinkGateway = arg(req, "tpGw");
    cfg_.fritzboxSsid  = arg(req, "fbSsid");
    String fbPass      = arg(req, "fbPass");
    if (fbPass.length()) cfg_.fritzboxPass = fbPass;
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
