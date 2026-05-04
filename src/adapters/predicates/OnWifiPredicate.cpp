#include "core/Predicate.h"
#include "core/Registry.h"
#include "NetworkManager.h"

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"target", FieldType::Enum, "Network", "fritzbox", "tplink,fritzbox", true},
};
constexpr PredicateSchema SCH = {"on-wifi", "On WiFi network", FIELDS, 1};

class OnWifiPredicate : public Predicate {
public:
    const PredicateSchema& schema() const override { return SCH; }
    bool test(JsonVariantConst params, RunCtx& ctx) override {
        if (!ctx.net) return false;
        const char* t = params["target"].as<const char*>();
        WifiTarget target = (t && std::string(t) == "tplink") ? WifiTarget::TpLink : WifiTarget::Fritzbox;
        return ctx.net->current() == target;
    }
};

OnWifiPredicate& instance(){ static OnWifiPredicate i; return i; }

}

namespace seqb { void registerOnWifiPredicate() { Registry::instance().registerPredicate(&instance()); } }
