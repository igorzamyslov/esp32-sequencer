#pragma once
#include "Schema.h"
#include "Sequence.h"
#include <ArduinoJson.h>
#include <map>
#include <string>
#include <vector>

class Config;          // fwd, defined in Arduino code
class NetworkManager;  // fwd
class StatusLed;       // fwd

namespace seqb {

struct RunCtx {
    Config* config = nullptr;
    NetworkManager* net = nullptr;
    StatusLed* led = nullptr;
    std::map<std::string, std::string> scratch;
};

enum class RunStatus { Ok, Failed };
struct RunResult {
    RunStatus status;
    std::string error;
    static RunResult ok() { return {RunStatus::Ok, ""}; }
    static RunResult failed(const std::string& why) { return {RunStatus::Failed, why}; }
};

class Interpreter;  // fwd

class Block {
public:
    virtual ~Block() = default;
    virtual const BlockSchema& schema() const = 0;
    virtual RunResult run(JsonVariantConst params,
                          const std::map<std::string, std::vector<Node>>& children,
                          RunCtx& ctx,
                          Interpreter& interp) = 0;
};

}  // namespace seqb
