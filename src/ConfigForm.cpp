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
<p class=hint>Bootstrap only: credentials for the WiFi network where this device idles and serves the web UI. Any other networks the device hops to are configured as <code>wifi-hop</code> blocks in the sequence editor.</p>
<form method=post action=/save>
<fieldset><legend>Idle WiFi</legend>
SSID <input name=idleSsid value="{{idleSsid}}">
Password <input name=idlePass type=password placeholder="{{idlePassHint}}">
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
    html.replace("{{idleSsid}}", htmlAttrEscape(cfg.idleSsid));
    html.replace("{{idlePassHint}}", cfg.idlePass.length() ? "(saved — leave empty to keep)" : "");
    html.replace("{{sFbChecked}}", cfg.setupFallback ? "checked" : "");
    return html;
}

void ConfigForm::applySave(AsyncWebServerRequest* req, Config& cfg) {
    cfg.idleSsid     = arg(req, "idleSsid");
    String idlePass  = arg(req, "idlePass");
    if (idlePass.length()) cfg.idlePass = idlePass;
    cfg.setupFallback = req->hasParam("sFb", true);
    cfg.save();
}
