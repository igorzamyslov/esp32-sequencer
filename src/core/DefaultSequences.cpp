#include "DefaultSequences.h"

namespace seqb {

static Node leaf(const char* type) { Node n; n.type = type; return n; }

DefaultsResult buildDefaults(const std::string& pcMac,
                             const std::string& tvMac,
                             const std::string& dualsenseMac) {
    DefaultsResult r;
    Sequence wake;
    wake.id = "wake-everything";
    wake.name = "Wake everything";
    wake.cooldownMs = 60000;

    {
        Node n = leaf("wifi-hop"); n.params["target"] = "tplink"; wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol"); n.params["mac"] = pcMac; wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wifi-hop"); n.params["target"] = "fritzbox"; wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol"); n.params["mac"] = tvMac; wake.nodes.push_back(n);
    }
    {
        Node n = leaf("samsung-keys");
        n.params["keys"] = "KEY_SOURCE";
        n.params["settle_ms"] = 900;
        wake.nodes.push_back(n);
    }
    {
        Node rep = leaf("repeat");
        rep.params["count"] = 6;
        Node leftKey = leaf("samsung-keys");
        leftKey.params["keys"] = "KEY_LEFT";
        leftKey.params["settle_ms"] = 350;
        rep.children["body"].push_back(leftKey);
        wake.nodes.push_back(rep);
    }
    {
        Node n = leaf("samsung-keys");
        n.params["keys"] = "KEY_RIGHT,KEY_RIGHT,KEY_RIGHT,KEY_ENTER";
        n.params["settle_ms"] = 350;
        wake.nodes.push_back(n);
    }
    r.sequences.push_back(std::move(wake));

    if (!dualsenseMac.empty()) {
        TriggerBinding b;
        b.id = "default-ble";
        b.type = "ble-mac";
        b.params["mac"] = dualsenseMac;
        b.params["cooldown_ms"] = 60000;
        b.sequenceId = "wake-everything";
        b.enabled = true;
        r.triggers.push_back(b);
    }
    {
        TriggerBinding b;
        b.id = "default-trigger";
        b.type = "http-route";
        b.params["path"] = "/trigger";
        b.sequenceId = "wake-everything";
        b.enabled = true;
        r.triggers.push_back(b);
    }
    return r;
}

}
