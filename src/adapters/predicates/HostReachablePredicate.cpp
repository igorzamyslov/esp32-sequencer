#include "core/Predicate.h"
#include "core/Registry.h"
#include <WiFi.h>
#include <WiFiClient.h>

using namespace seqb;
namespace {

constexpr FieldDef FIELDS[] = {
    {"ip", FieldType::String, "IP address", nullptr, nullptr, true},
    {"port", FieldType::Int, "TCP port (default 80)", "80", nullptr, false},
    {"timeout_ms", FieldType::Int, "Timeout (ms)", "500", nullptr, false},
};
constexpr PredicateSchema SCH = {"host-reachable", "Host reachable (TCP)", FIELDS, 3};

class HostReachablePredicate : public Predicate {
public:
    const PredicateSchema& schema() const override { return SCH; }
    bool test(JsonVariantConst params, RunCtx&) override {
        const char* ip = params["ip"].as<const char*>();
        if (!ip || !ip[0]) return false;
        uint16_t port = params["port"] | 80;
        uint32_t to = params["timeout_ms"] | 500;
        WiFiClient c;
        c.setTimeout(to);
        bool ok = c.connect(ip, port, to);
        if (ok) c.stop();
        return ok;
    }
};

HostReachablePredicate& instance() {
    static HostReachablePredicate i;
    return i;
}
struct _Reg {
    _Reg() { Registry::instance().registerPredicate(&instance()); }
} _reg;

}  // namespace
