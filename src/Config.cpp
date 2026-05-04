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
    prefs().begin(NS, true); // read-only
    c.tplinkSsid    = prefs().getString("tpSsid", "");
    c.tplinkPass    = prefs().getString("tpPass", "");
    c.tplinkStaticIp= prefs().getString("tpIp", "");
    c.tplinkGateway = prefs().getString("tpGw", "");
    c.fritzboxSsid  = prefs().getString("fbSsid", "");
    c.fritzboxPass  = prefs().getString("fbPass", "");
    c.tvToken       = prefs().getString("tvTok", "");
    c.setupFallback = prefs().getBool("sFb", false);
    prefs().end();
    return c;
}

void Config::save() const {
    prefs().begin(NS, false); // read-write
    prefs().putString("tpSsid", tplinkSsid);
    prefs().putString("tpPass", tplinkPass);
    prefs().putString("tpIp", tplinkStaticIp);
    prefs().putString("tpGw", tplinkGateway);
    prefs().putString("fbSsid", fritzboxSsid);
    prefs().putString("fbPass", fritzboxPass);
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
