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
        Node n = leaf("wifi-hop");
        n.params["_label"] = "Hop to PC's network";
        wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol");
        n.params["_label"] = "Wake PC";
        n.params["mac"] = "";
        wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wifi-hop");
        n.params["_label"] = "Hop back to idle network";
        wake.nodes.push_back(n);
    }
    {
        Node n = leaf("wol");
        n.params["_label"] = "Wake TV";
        n.params["mac"] = "";
        wake.nodes.push_back(n);
    }
    {
        Node n = leaf("divider");
        n.params["text"] = "Switch to HDMI input";
        wake.nodes.push_back(n);
    }
    {
        Node n = leaf("samsung-key"); n.params["ip"] = ""; n.params["key"] = "KEY_SOURCE";
        wake.nodes.push_back(n);
    }
    {
        Node w = leaf("wait"); w.params["ms"] = 900;
        wake.nodes.push_back(w);
    }
    {
        Node rep = leaf("repeat");
        rep.params["count"] = 6;
        rep.params["interval_ms"] = 350;
        rep.params["_label"] = "Mash LEFT to reach the leftmost source";
        Node k = leaf("samsung-key"); k.params["ip"] = ""; k.params["key"] = "KEY_LEFT";
        rep.children["body"].push_back(k);
        wake.nodes.push_back(rep);
    }
    {
        Node rep = leaf("repeat");
        rep.params["count"] = 3;
        rep.params["interval_ms"] = 350;
        rep.params["_label"] = "Step right N times to reach HDMIn";
        Node k = leaf("samsung-key"); k.params["ip"] = ""; k.params["key"] = "KEY_RIGHT";
        rep.children["body"].push_back(k);
        wake.nodes.push_back(rep);
    }
    {
        Node n = leaf("samsung-key"); n.params["ip"] = ""; n.params["key"] = "KEY_ENTER";
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
