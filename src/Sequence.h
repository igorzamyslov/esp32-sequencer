#pragma once
#include "Config.h"
#include "NetworkManager.h"
#include "TvController.h"
#include "StatusLed.h"

struct SequenceDeps {
    Config* config;
    NetworkManager* net;
    StatusLed* led;
};

class Sequence {
public:
    explicit Sequence(SequenceDeps d) : d_(d) {}
    bool run(); // true on success
private:
    SequenceDeps d_;
};
