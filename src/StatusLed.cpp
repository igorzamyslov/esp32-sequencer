#include "StatusLed.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

StatusLed::StatusLed() : state_(LedState::Setup), state_entered_at_(0) {}

void StatusLed::setState(LedState s) {
    state_ = s;
    state_entered_at_ = 0;
    entered_ = false;
}

static bool computeIsOn(LedState s, uint32_t t) {
    switch (s) {
        case LedState::Setup: {
            // 1 Hz: 500ms on, 500ms off
            return (t % 1000) < 500;
        }
        case LedState::Idle: {
            // 100ms blip every 3000ms
            return (t % 3000) < 100;
        }
        case LedState::Running: {
            // 5 Hz: 100ms on, 100ms off
            return (t % 200) < 100;
        }
        case LedState::Success: {
            // Solid for 2000ms (caller transitions out)
            return t < 2000;
        }
        case LedState::Error: {
            // Three 100ms pulses, 100ms gaps; off after 600ms (caller transitions out)
            if (t >= 600) return false;
            uint32_t slot = t / 100;
            return (slot % 2) == 0;
        }
    }
    return false;
}

bool StatusLed::isOnAt(uint32_t ms_since_state_entry) const {
    return computeIsOn(state_, ms_since_state_entry);
}

void StatusLed::tick(uint32_t now_ms) {
    // Anchor state_entered_at_ to now_ms on the first tick after setState.
    // We must not conflate "haven't been ticked yet" with "state entered at t=0",
    // or in production (where millis() is large) Success/Error would auto-transition
    // instantly and Idle's heartbeat would be phase-shifted.
    if (!entered_) {
        state_entered_at_ = now_ms;
        entered_ = true;
    }
    uint32_t elapsed = now_ms - state_entered_at_;

    if (state_ == LedState::Success && elapsed >= 2000) {
        setState(LedState::Idle);
        // Re-anchor immediately so we don't lose this tick to the !entered_ branch.
        state_entered_at_ = now_ms;
        entered_ = true;
        elapsed = 0;
    } else if (state_ == LedState::Error && elapsed >= 600) {
        setState(LedState::Idle);
        state_entered_at_ = now_ms;
        entered_ = true;
        elapsed = 0;
    }

    bool on = computeIsOn(state_, elapsed);
    last_written_ = on;

#ifdef ARDUINO
    if (pin_ >= 0) {
        // Active LOW: drive pin LOW to turn the LED on
        digitalWrite(pin_, on ? LOW : HIGH);
    }
#endif
}

bool StatusLed::currentlyOn() const { return last_written_; }

#ifdef ARDUINO
void StatusLed::attachPin(int pin) {
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, HIGH); // off (active LOW)
}
#endif
