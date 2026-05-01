#pragma once
#include <stdint.h>

enum class LedState { Setup, Idle, Running, Success, Error };

class StatusLed {
public:
    StatusLed();
    void setState(LedState s);
    LedState state() const { return state_; }
    // For tests: deterministic timeline starting at the moment setState() was called.
    bool isOnAt(uint32_t ms_since_state_entry) const;
    // Production path: pass current millis(); auto-transitions Success/Error → Idle.
    void tick(uint32_t now_ms);
    bool currentlyOn() const;

#ifdef ARDUINO
    void attachPin(int pin);
#endif

private:
    LedState state_ = LedState::Setup;
    uint32_t state_entered_at_ = 0;
    int pin_ = -1;
    bool last_written_ = false;
};
