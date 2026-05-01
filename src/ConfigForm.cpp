#include "ConfigForm.h"
#include "Config.h"
#include <ESPAsyncWebServer.h>

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
{{pairSection}}
</fieldset>
<fieldset><legend>Recovery</legend>
<label><input type=checkbox name=sFb value=1 {{sFbChecked}}>
Fall back to setup AP if WiFi stays unreachable</label>
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

    const char* PAIR_SECTION = R"HTML(
<button type=button onclick="startPair()">Pair gamepad</button>
<span id=pairStatus></span>
)HTML";

    String htmlAttrEscape(const String& s) {
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

    String arg(AsyncWebServerRequest* req, const char* name) {
        if (!req->hasParam(name, true)) return "";
        return req->getParam(name, true)->value();
    }
}

String ConfigForm::renderHtml(const Config& cfg, bool include_pair) {
    String html = INDEX_HTML;
    html.replace("{{tpSsid}}", htmlAttrEscape(cfg.tplinkSsid));
    html.replace("{{tpIp}}",   htmlAttrEscape(cfg.tplinkStaticIp));
    html.replace("{{tpGw}}",   htmlAttrEscape(cfg.tplinkGateway));
    html.replace("{{fbSsid}}", htmlAttrEscape(cfg.fritzboxSsid));
    html.replace("{{pcMac}}",  htmlAttrEscape(cfg.pcMac));
    html.replace("{{tvIp}}",   htmlAttrEscape(cfg.tvIp));
    html.replace("{{tvMac}}",  htmlAttrEscape(cfg.tvMac));
    html.replace("{{dsMac}}",  htmlAttrEscape(cfg.dualsenseMac));
    html.replace("{{tpPassHint}}", cfg.tplinkPass.length()   ? "(saved — leave empty to keep)" : "");
    html.replace("{{fbPassHint}}", cfg.fritzboxPass.length() ? "(saved — leave empty to keep)" : "");
    html.replace("{{sFbChecked}}", cfg.setupFallback ? "checked" : "");
    html.replace("{{pairSection}}", include_pair ? PAIR_SECTION : "");
    return html;
}

void ConfigForm::applySave(AsyncWebServerRequest* req, Config& cfg) {
    cfg.tplinkSsid     = arg(req, "tpSsid");
    String tpPass      = arg(req, "tpPass");
    if (tpPass.length()) cfg.tplinkPass = tpPass;
    cfg.tplinkStaticIp = arg(req, "tpIp");
    cfg.tplinkGateway  = arg(req, "tpGw");
    cfg.fritzboxSsid   = arg(req, "fbSsid");
    String fbPass      = arg(req, "fbPass");
    if (fbPass.length()) cfg.fritzboxPass = fbPass;
    cfg.pcMac          = arg(req, "pcMac");
    cfg.tvIp           = arg(req, "tvIp");
    cfg.tvMac          = arg(req, "tvMac");
    cfg.dualsenseMac   = arg(req, "dsMac");
    cfg.setupFallback  = req->hasParam("sFb", true);
    cfg.save();
}
