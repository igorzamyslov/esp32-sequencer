#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"url", FieldType::String, "URL (ws:// or wss://)", nullptr, nullptr, true},
    {"message", FieldType::String, "Message", "", nullptr, true},
    {"wait_response_ms", FieldType::Int, "Wait for response (ms, 0 = none)", "0", nullptr, false},
    {"timeout_ms", FieldType::Int, "Connect timeout (ms)", "5000", nullptr, false},
};
constexpr BlockSchema SCH = {"ws-send", "WebSocket send", "Network", FIELDS, 4, nullptr, 0};

// File-scoped state for the static event callback. WebSocketsClient is
// allocated per-call so we can connect to arbitrary endpoints; the callback
// must be a free function, so it writes through these flags. We reset them
// at the start of each run().
volatile bool g_connected = false;
volatile bool g_gotMessage = false;
volatile bool g_disconnected = false;

void onEvent(WStype_t type, uint8_t* payload, size_t len) {
    (void)payload;
    (void)len;
    switch (type) {
        case WStype_CONNECTED:
            g_connected = true;
            break;
        case WStype_DISCONNECTED:
            g_disconnected = true;
            g_connected = false;
            break;
        case WStype_TEXT:
        case WStype_BIN:
            g_gotMessage = true;
            break;
        default:
            break;
    }
}

// Parse "ws://host:port/path" or "wss://host:port/path". Defaults: 80/443,
// path "/". Returns false if the scheme is unrecognised.
bool parseWsUrl(const String& url, bool& isSecure, String& host, uint16_t& port, String& path) {
    int schemeEnd;
    if (url.startsWith("wss://")) {
        isSecure = true;
        schemeEnd = 6;
    } else if (url.startsWith("ws://")) {
        isSecure = false;
        schemeEnd = 5;
    } else {
        return false;
    }
    int pathStart = url.indexOf('/', schemeEnd);
    String authority =
        pathStart < 0 ? url.substring(schemeEnd) : url.substring(schemeEnd, pathStart);
    path = pathStart < 0 ? String("/") : url.substring(pathStart);
    int colon = authority.indexOf(':');
    if (colon < 0) {
        host = authority;
        port = isSecure ? 443 : 80;
    } else {
        host = authority.substring(0, colon);
        port = (uint16_t)authority.substring(colon + 1).toInt();
        if (port == 0) port = isSecure ? 443 : 80;
    }
    return host.length() > 0;
}

class WsSendBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        const char* urlRaw = params["url"].as<const char*>();
        const char* message = params["message"] | "";
        uint32_t waitMs = params["wait_response_ms"] | 0;
        uint32_t timeoutMs = params["timeout_ms"] | 5000;
        if (!urlRaw || !urlRaw[0]) return RunResult::failed("ws-send: missing url");
        if (WiFi.status() != WL_CONNECTED) return RunResult::failed("ws-send: wifi not connected");

        bool isSecure;
        String host, path;
        uint16_t port;
        if (!parseWsUrl(String(urlRaw), isSecure, host, port, path))
            return RunResult::failed("ws-send: bad url (need ws:// or wss://)");

        g_connected = false;
        g_gotMessage = false;
        g_disconnected = false;

        WebSocketsClient ws;
        ws.onEvent(&onEvent);
        if (isSecure)
            ws.beginSslWithCA(host.c_str(), port, path.c_str(), nullptr, "");
        else
            ws.begin(host, port, path);

        uint32_t start = millis();
        while (!g_connected && millis() - start < timeoutMs) {
            ws.loop();
            if (g_disconnected) break;
            delay(10);
        }
        if (!g_connected) {
            ws.disconnect();
            return RunResult::failed("ws-send: connect failed");
        }

        bool sent = ws.sendTXT(message);
        Serial.printf(
            "[ws-send] %s -> sent=%d (%u bytes)\n", urlRaw, sent, (unsigned)strlen(message));

        // Pump briefly so the frame actually leaves before we close.
        uint32_t flushUntil = millis() + 80;
        while (millis() < flushUntil) {
            ws.loop();
            delay(10);
        }

        if (waitMs > 0) {
            uint32_t until = millis() + waitMs;
            while (!g_gotMessage && millis() < until) {
                ws.loop();
                if (g_disconnected) break;
                delay(10);
            }
        }

        ws.disconnect();
        if (!sent) return RunResult::failed("ws-send: send failed");
        return RunResult::ok();
    }
};

WsSendBlock& instance() {
    static WsSendBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
