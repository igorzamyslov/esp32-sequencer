#pragma once
#include <Arduino.h>

struct Config {
    String tplinkSsid;
    String tplinkPass;
    String fritzboxSsid;
    String fritzboxPass;
    String pcMac;          // "AA:BB:CC:DD:EE:FF"
    String tvIp;           // "192.168.178.42"
    String tvMac;
    String tvToken;        // empty until pairing succeeds
    String dualsenseMac;

    static Config load();
    void save() const;
    static void clear();

    // Has every value needed to enter runtime mode.
    bool hasAll() const {
        return tplinkSsid.length() && tplinkPass.length()
            && fritzboxSsid.length() && fritzboxPass.length()
            && pcMac.length()
            && tvIp.length() && tvMac.length()
            && dualsenseMac.length();
        // tvToken is allowed to be empty; the first runtime sequence will
        // attempt to pair, but the user generally pairs explicitly during setup.
    }
};
