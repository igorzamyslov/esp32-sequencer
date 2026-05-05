#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include "TvController.h"
#include "Config.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"ip", FieldType::String, "TV IP address", nullptr, nullptr, true},
    {"key", FieldType::String, "Tizen key (e.g. KEY_SOURCE)", nullptr, nullptr, true},
    {"connect_timeout_ms", FieldType::Int, "Connect timeout (ms)", "60000", nullptr, false},
};
constexpr BlockSchema SCH = {
    "samsung-key",
    "Samsung TV: send key",
    "TV (Samsung)",
    FIELDS,
    3,
    nullptr,
    0,
};

// Persistent session — connecting to the TV's :8002 WS is the slow part
// (several seconds, more if the TV was asleep). We keep one open across
// invocations so successive `samsung-key` blocks (or one inside a Repeat) only
// pay the cost once. The session is reused while the IP is unchanged and
// recent traffic is within REUSE_WINDOW_MS; otherwise we reconnect.
constexpr uint32_t REUSE_WINDOW_MS = 30000;

TvController& tv() {
    static TvController t;
    return t;
}
String& tvIp() {
    static String s;
    return s;
}
uint32_t& tvLastMs() {
    static uint32_t v = 0;
    return v;
}

class SamsungKeyBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx& ctx,
                  Interpreter&) override {
        if (!ctx.config) return RunResult::failed("samsung-key: no config");
        const char* ip = params["ip"].as<const char*>();
        const char* key = params["key"].as<const char*>();
        if (!ip || !ip[0]) return RunResult::failed("samsung-key: missing ip");
        if (!key || !key[0]) return RunResult::failed("samsung-key: missing key");
        uint32_t toMs = params["connect_timeout_ms"] | 60000;

        bool needConnect = (tvIp() != String(ip)) || (tvIp().length() == 0) ||
                           (millis() - tvLastMs() > REUSE_WINDOW_MS);
        if (needConnect) {
            if (tvIp().length()) tv().disconnect();
            tvIp() = "";
            tv().configure(String(ip), ctx.config->tvToken, [&ctx](const String& t) {
                if (t != ctx.config->tvToken) {
                    ctx.config->tvToken = t;
                    ctx.config->save();
                }
            });
            if (!tv().connectWithRetry(toMs))
                return RunResult::failed("samsung-key: ws connect failed");
            tvIp() = String(ip);
            tv().pump(200);
        } else {
            tv().pump(20);  // service the socket
        }

        if (!tv().sendKey(key)) {
            // session may have died — invalidate so next call reconnects
            tvIp() = "";
            return RunResult::failed("samsung-key: send failed");
        }
        tv().pump(80);
        tvLastMs() = millis();
        return RunResult::ok();
    }
};

SamsungKeyBlock& instance() {
    static SamsungKeyBlock i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerBlock(&instance()); }
} _reg;

}  // namespace
