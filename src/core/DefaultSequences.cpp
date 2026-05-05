#include "DefaultSequences.h"

namespace seqb {

static Node leaf(const char* type) {
    Node n;
    n.type = type;
    return n;
}

// Self-contained demo: shows label, divider, wait, and repeat with an interval.
// Uses no external hardware so it runs cleanly on a fresh device. Edit or
// delete; replace with real blocks (wol, wifi-hop, samsung-key, …) for your setup.
DefaultsResult buildDefaults() {
    DefaultsResult r;
    Sequence ex;
    ex.id = "example";
    ex.name = "Example sequence";
    ex.cooldownMs = 30000;

    {
        Node n = leaf("divider");
        n.params["text"] = "Demo — edit, delete, or build your own";
        ex.nodes.push_back(n);
    }
    {
        Node n = leaf("wait");
        n.params["ms"] = 500;
        n.params["_label"] = "Pause briefly";
        ex.nodes.push_back(n);
    }
    {
        Node rep = leaf("repeat");
        rep.params["count"] = 3;
        rep.params["interval_ms"] = 250;
        rep.params["_label"] = "Three short pulses, 250ms apart";
        Node w = leaf("wait");
        w.params["ms"] = 50;
        rep.children["body"].push_back(w);
        ex.nodes.push_back(rep);
    }
    {
        Node n = leaf("divider");
        n.params["text"] = "Add real blocks (wol, wifi-hop, samsung-key, …) to drive hardware";
        ex.nodes.push_back(n);
    }
    r.sequences.push_back(std::move(ex));

    {
        TriggerBinding b;
        b.id = "default-trigger";
        b.type = "http-route";
        b.params["path"] = "/trigger";
        b.sequenceId = "example";
        b.enabled = true;
        r.triggers.push_back(b);
    }
    return r;
}

}  // namespace seqb
