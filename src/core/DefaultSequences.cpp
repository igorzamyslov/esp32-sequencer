#include "DefaultSequences.h"

namespace seqb {

static Node leaf(const char* type) { Node n; n.type = type; return n; }

DefaultsResult buildDefaults() {
    DefaultsResult r;
    Sequence wake;
    wake.id = "wake-everything";
    wake.name = "Wake everything";
    wake.cooldownMs = 60000;

    {
        // Hop to the network where the PC is reachable for WoL. Fill ssid/password
        // (and optional static_ip/gateway) in the editor.
        Node n = leaf("wifi-hop"); wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol"); n.params["mac"] = ""; wake.nodes.push_back(n);
    }
    {
        // Hop back to the idle network. Fill in the same SSID/password as in /settings.
        Node n = leaf("wifi-hop"); wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol"); n.params["mac"] = ""; wake.nodes.push_back(n);
    }
    {
        Node n = leaf("samsung-keys");
        n.params["ip"] = "";
        n.params["keys"] = "KEY_SOURCE";
        n.params["settle_ms"] = 900;
        wake.nodes.push_back(n);
    }
    {
        Node rep = leaf("repeat");
        rep.params["count"] = 6;
        Node leftKey = leaf("samsung-keys");
        leftKey.params["ip"] = "";
        leftKey.params["keys"] = "KEY_LEFT";
        leftKey.params["settle_ms"] = 350;
        rep.children["body"].push_back(leftKey);
        wake.nodes.push_back(rep);
    }
    {
        Node n = leaf("samsung-keys");
        n.params["ip"] = "";
        n.params["keys"] = "KEY_RIGHT,KEY_RIGHT,KEY_RIGHT,KEY_ENTER";
        n.params["settle_ms"] = 350;
        wake.nodes.push_back(n);
    }
    r.sequences.push_back(std::move(wake));

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
