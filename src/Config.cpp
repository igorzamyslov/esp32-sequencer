#include "Config.h"
#include <Preferences.h>

namespace {
    constexpr const char* NS = "esp32tv";
    Preferences& prefs() {
        static Preferences p;
        return p;
    }
}

Config Config::load() {
    Config c;
    prefs().begin(NS, true);
    c.idleSsid      = prefs().getString("idleSsid", "");
    c.idlePass      = prefs().getString("idlePass", "");
    c.tvToken       = prefs().getString("tvTok", "");
    c.setupFallback = prefs().getBool("sFb", false);
    prefs().end();
    return c;
}

void Config::save() const {
    prefs().begin(NS, false);
    prefs().putString("idleSsid", idleSsid);
    prefs().putString("idlePass", idlePass);
    prefs().putString("tvTok", tvToken);
    prefs().putBool("sFb", setupFallback);
    prefs().end();
}

void Config::clear() {
    prefs().begin(NS, false);
    prefs().clear();
    prefs().end();
}

void Config::requestSetupOnNextBoot() {
    prefs().begin(NS, false);
    prefs().putBool("forceSetup", true);
    prefs().end();
}

bool Config::consumeSetupRequest() {
    prefs().begin(NS, false);
    bool v = prefs().getBool("forceSetup", false);
    if (v) prefs().remove("forceSetup");
    prefs().end();
    return v;
}
