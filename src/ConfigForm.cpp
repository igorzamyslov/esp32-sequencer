#include "ConfigForm.h"
#include "Config.h"
#include <ESPAsyncWebServer.h>

namespace {
    const char* INDEX_HTML = R"HTML(
<!doctype html><meta charset=utf-8><title>esp32-tv setup</title>
<style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}
input{width:100%;padding:.4em;margin:.2em 0;box-sizing:border-box}
button{padding:.5em 1em;margin:.4em 0}
fieldset{margin:1em 0}
.hint{font-size:.85em;color:#666;margin:.4em 0}</style>
<h1>esp32-tv setup</h1>
<p class=hint>Bootstrap-only: WiFi credentials. Per-device parameters (PC MAC, TV IP/MAC, DualSense MAC) are configured in the sequence editor at <code>/edit</code> and <code>/triggers</code> after first boot.</p>
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
<fieldset><legend>Recovery</legend>
<label><input type=checkbox name=sFb value=1 {{sFbChecked}}>
Fall back to setup AP if WiFi stays unreachable</label>
</fieldset>
<button type=submit>Save and reboot</button>
</form>
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

String ConfigForm::renderHtml(const Config& cfg, bool /*include_pair_unused*/) {
    String html = INDEX_HTML;
    html.replace("{{tpSsid}}", htmlAttrEscape(cfg.tplinkSsid));
    html.replace("{{tpIp}}",   htmlAttrEscape(cfg.tplinkStaticIp));
    html.replace("{{tpGw}}",   htmlAttrEscape(cfg.tplinkGateway));
    html.replace("{{fbSsid}}", htmlAttrEscape(cfg.fritzboxSsid));
    html.replace("{{tpPassHint}}", cfg.tplinkPass.length()   ? "(saved — leave empty to keep)" : "");
    html.replace("{{fbPassHint}}", cfg.fritzboxPass.length() ? "(saved — leave empty to keep)" : "");
    html.replace("{{sFbChecked}}", cfg.setupFallback ? "checked" : "");
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
    cfg.setupFallback  = req->hasParam("sFb", true);
    cfg.save();
}
