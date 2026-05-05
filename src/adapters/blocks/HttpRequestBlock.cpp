#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"url", FieldType::String, "URL (http:// or https://)", nullptr, nullptr, true},
    {"method", FieldType::Enum, "Method", "GET", "GET,POST,PUT,DELETE,PATCH", false},
    {"body", FieldType::String, "Request body", "", nullptr, false},
    {"content_type", FieldType::String, "Content-Type", "application/json", nullptr, false},
    {"headers",
     FieldType::StringList,
     "Extra headers (one per line: Name: Value)",
     "",
     nullptr,
     false},
    {"timeout_ms", FieldType::Int, "Timeout (ms)", "5000", nullptr, false},
    {"expected_status", FieldType::Int, "Expected status (0 = any 2xx)", "0", nullptr, false},
};
constexpr BlockSchema SCH = {
    "http-request",
    "HTTP request",
    "Network",
    FIELDS,
    7,
    nullptr,
    0,
};

// Parse "Name: Value" lines (newline- or comma-separated) and add to req.
void addHeaders(HTTPClient& http, const char* raw) {
    if (!raw || !raw[0]) return;
    String s(raw);
    int from = 0;
    while (from <= (int)s.length()) {
        int nl = s.indexOf('\n', from);
        int cm = s.indexOf(',', from);
        int end;
        if (nl < 0 && cm < 0)
            end = s.length();
        else if (nl < 0)
            end = cm;
        else if (cm < 0)
            end = nl;
        else
            end = nl < cm ? nl : cm;
        String line = s.substring(from, end);
        line.trim();
        if (line.length()) {
            int colon = line.indexOf(':');
            if (colon > 0) {
                String name = line.substring(0, colon);
                String value = line.substring(colon + 1);
                name.trim();
                value.trim();
                http.addHeader(name, value);
            }
        }
        from = end + 1;
    }
}

class HttpRequestBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx&,
                  Interpreter&) override {
        const char* url = params["url"].as<const char*>();
        if (!url || !url[0]) return RunResult::failed("http: missing url");
        const char* method = params["method"] | "GET";
        const char* body = params["body"] | "";
        const char* contentType = params["content_type"] | "application/json";
        const char* headers = params["headers"] | "";
        uint32_t timeoutMs = params["timeout_ms"] | 5000;
        int expected = params["expected_status"] | 0;

        if (WiFi.status() != WL_CONNECTED) return RunResult::failed("http: wifi not connected");

        bool isHttps = (strncmp(url, "https://", 8) == 0);
        WiFiClient plain;
        WiFiClientSecure tls;
        if (isHttps) tls.setInsecure();

        HTTPClient http;
        bool began = isHttps ? http.begin(tls, url) : http.begin(plain, url);
        if (!began) return RunResult::failed("http: begin failed");
        http.setTimeout(timeoutMs);
        http.setConnectTimeout(timeoutMs);

        bool hasBody = body && body[0];
        if (hasBody) http.addHeader("Content-Type", contentType);
        addHeaders(http, headers);

        int code = -1;
        String m(method);
        m.toUpperCase();
        if (m == "GET")
            code = http.GET();
        else if (m == "POST")
            code = http.POST((uint8_t*)body, hasBody ? strlen(body) : 0);
        else if (m == "PUT")
            code = http.PUT((uint8_t*)body, hasBody ? strlen(body) : 0);
        else if (m == "DELETE")
            code = http.sendRequest("DELETE", (uint8_t*)body, hasBody ? strlen(body) : 0);
        else if (m == "PATCH")
            code = http.sendRequest("PATCH", (uint8_t*)body, hasBody ? strlen(body) : 0);
        else {
            http.end();
            return RunResult::failed(std::string("http: unsupported method ") + method);
        }

        Serial.printf("[http] %s %s -> %d\n", m.c_str(), url, code);
        http.end();

        if (code <= 0) {
            return RunResult::failed(std::string("http: transport error ") + std::to_string(code));
        }
        bool ok = expected ? (code == expected) : (code >= 200 && code < 300);
        if (!ok) {
            return RunResult::failed(std::string("http: status ") + std::to_string(code));
        }
        return RunResult::ok();
    }
};

HttpRequestBlock& instance() {
    static HttpRequestBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
