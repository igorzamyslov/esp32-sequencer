#include "core/Predicate.h"
#include "core/Registry.h"
#include "NetworkManager.h"

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"ssid", FieldType::String, "SSID", nullptr, nullptr, true},
};
constexpr PredicateSchema SCH = {"on-wifi", "On WiFi network", FIELDS, 1};

class OnWifiPredicate : public Predicate {
public:
    const PredicateSchema& schema() const override { return SCH; }
    bool test(JsonVariantConst params, RunCtx& ctx) override {
        if (!ctx.net) return false;
        const char* ssid = params["ssid"].as<const char*>();
        if (!ssid) return false;
        return ctx.net->isConnected() && ctx.net->currentSsid() == String(ssid);
    }
};

OnWifiPredicate& instance(){ static OnWifiPredicate i; return i; }
struct _Reg { _Reg(){ Registry::instance().registerPredicate(&instance()); } } _reg;

}
