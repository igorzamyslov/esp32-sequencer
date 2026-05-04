#include "core/Block.h"
#include "core/Registry.h"
#include "core/Interpreter.h"
#include "TvController.h"
#include "Config.h"
#include <Arduino.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"keys",      FieldType::StringList, "Keys (comma-separated)", nullptr, nullptr, true},
    {"settle_ms", FieldType::Int,        "Settle ms after each key", "250", nullptr, false},
    {"connect_timeout_ms", FieldType::Int, "Connect timeout (ms)",  "60000", nullptr, false},
};
constexpr BlockSchema SCH = {
    "samsung-keys", "Samsung TV: send keys", "TV (Samsung)", FIELDS, 3, nullptr, 0,
};

class SamsungTizenKeysBlock : public Block {
public:
    const BlockSchema& schema() const override { return SCH; }
    RunResult run(JsonVariantConst params,
                  const std::map<std::string, std::vector<Node>>&,
                  RunCtx& ctx, Interpreter&) override {
        if (!ctx.config) return RunResult::failed("samsung-keys: no config");
        const char* csv = params["keys"].as<const char*>();
        if (!csv || !csv[0]) return RunResult::failed("samsung-keys: missing keys");
        uint32_t settle = params["settle_ms"] | 250;
        uint32_t toMs   = params["connect_timeout_ms"] | 60000;

        TvController tv;
        bool tokenChanged = false;
        String newToken;
        tv.configure(ctx.config->tvIp, ctx.config->tvToken,
                     [&](const String& t){ tokenChanged = true; newToken = t; });
        if (!tv.connectWithRetry(toMs)) return RunResult::failed("samsung-keys: ws connect failed");
        tv.pump(200);

        String s(csv);
        int from = 0;
        while (from <= (int)s.length()) {
            int comma = s.indexOf(',', from);
            String one = comma < 0 ? s.substring(from) : s.substring(from, comma);
            one.trim();
            if (one.length()) { tv.sendKey(one.c_str()); tv.pump(settle); }
            if (comma < 0) break;
            from = comma + 1;
        }
        tv.pump(300);
        tv.disconnect();

        if (tokenChanged) {
            ctx.config->tvToken = newToken;
            ctx.config->save();
        }
        return RunResult::ok();
    }
};

SamsungTizenKeysBlock& instance(){ static SamsungTizenKeysBlock i; return i; }
struct _Reg { _Reg(){ Registry::instance().registerBlock(&instance()); } } _reg;

}
