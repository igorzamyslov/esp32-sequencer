#include "ConfigForm.h"
#include "Config.h"
#include <ESPAsyncWebServer.h>

namespace {
    const char* INDEX_HTML = R"HTML(
<!doctype html><meta charset=utf-8><title>esp32-sequencer setup</title>
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
:root{
  --bg:#0E1A20;--bg-2:#15242C;--bg-3:#1F313A;
  --fg:#D8DEE9;--fg-2:#A7ADBA;--fg-3:#65737E;
  --rule:#2C3D47;--accent:#6699CC;--accent-2:#62B3B2;--ok:#99C794;--danger:#EC5F67;
  --sans:-apple-system,BlinkMacSystemFont,"SF Pro Text","Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
  --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace;
  color-scheme:dark;
}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 var(--sans);-webkit-font-smoothing:antialiased}
.wrap{max-width:520px;margin:0 auto;padding:40px 24px 80px}
header{display:flex;justify-content:space-between;align-items:baseline;
  padding-bottom:14px;border-bottom:1px solid var(--rule);margin-bottom:24px}
header h1{font:600 22px/1.2 var(--sans);letter-spacing:-0.01em;margin:0}
header a{color:var(--fg);text-decoration:none;font:13px/1 var(--sans);font-weight:500;
  padding:7px 12px;border:1px solid var(--rule);border-radius:6px;background:var(--bg)}
header a:hover{background:var(--bg-2);border-color:var(--fg-3)}
.hint{font-size:13px;color:var(--fg-2);margin:0 0 22px;line-height:1.5}
.hint code{font:12px/1 var(--mono);background:var(--bg-2);padding:2px 6px;border-radius:3px;color:var(--accent-2)}
fieldset{border:1px solid var(--rule);border-radius:6px;padding:16px 18px 18px;margin:0 0 18px;background:var(--bg)}
legend{font:600 11px/1 var(--sans);text-transform:uppercase;letter-spacing:.12em;color:var(--accent-2);padding:0 8px}
label{display:block;font:600 11px/1 var(--sans);text-transform:uppercase;letter-spacing:.08em;color:var(--fg-2);margin:12px 0 5px}
label:first-of-type{margin-top:0}
input[type=text],input[type=password],input:not([type]){
  width:100%;padding:8px 10px;font:13px/1.4 var(--mono);
  background:var(--bg-2);border:1px solid var(--rule);border-radius:5px;color:var(--fg);
}
input[type=text]:focus,input[type=password]:focus,input:not([type]):focus{
  outline:none;border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset
}
input::placeholder{color:var(--fg-3)}
.tog{display:flex;align-items:center;gap:8px;font:13px/1.4 var(--sans);color:var(--fg);text-transform:none;letter-spacing:0;font-weight:400;margin:0;cursor:pointer}
.tog input{width:auto;margin:0}
button{font:13px/1 var(--sans);font-weight:600;padding:9px 16px;border-radius:6px;
  background:var(--accent);border:1px solid var(--accent);color:var(--bg);cursor:pointer;
  transition:background .12s,border-color .12s}
button:hover{background:#7faedb;border-color:#7faedb}
::selection{background:var(--accent);color:var(--bg)}
</style>
<div class=wrap>
<header>
<h1>esp32-sequencer setup</h1>
<a href=/>back</a>
</header>
<p class=hint>Bootstrap only: credentials for the WiFi network where this device idles and serves the web UI. Any other networks the device hops to are configured as <code>wifi-hop</code> blocks in the sequence editor.</p>
<form method=post action=/save>
<fieldset>
<legend>Idle WiFi</legend>
<label>SSID</label>
<input name=idleSsid value="{{idleSsid}}">
<label>Password</label>
<input name=idlePass type=password placeholder="{{idlePassHint}}">
</fieldset>
<fieldset>
<legend>Recovery</legend>
<label class=tog><input type=checkbox name=sFb value=1 {{sFbChecked}}>
Fall back to setup AP if WiFi stays unreachable</label>
</fieldset>
<button type=submit>Save and reboot</button>
</form>
</div>
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
