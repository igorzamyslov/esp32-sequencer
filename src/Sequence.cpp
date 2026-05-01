#include "Sequence.h"
#include "Wol.h"

bool Sequence::run() {
    d_.led->setState(LedState::Running);

    // 1. WoL → PC (still on TP-Link)
    if (d_.net->current() != WifiTarget::TpLink) {
        if (!d_.net->hopTo(WifiTarget::TpLink)) {
            Serial.println("[seq] tplink connect failed");
            d_.led->setState(LedState::Error);
            return false;
        }
    }
    if (!Wol::sendBroadcast(d_.config->pcMac.c_str())) {
        Serial.println("[seq] WoL→PC failed");
        d_.led->setState(LedState::Error);
        return false;
    }
    Serial.println("[seq] WoL→PC sent");

    // 2. Hop → Fritzbox
    if (!d_.net->hopTo(WifiTarget::Fritzbox)) {
        Serial.println("[seq] fritzbox connect failed");
        d_.led->setState(LedState::Error);
        return false;
    }

    // 3. WoL → TV (idempotent)
    if (!Wol::sendBroadcast(d_.config->tvMac.c_str())) {
        Serial.println("[seq] WoL→TV failed");
        d_.led->setState(LedState::Error);
        return false;
    }
    Serial.println("[seq] WoL→TV sent");

    // 4. WebSocket connect with retry up to 60 s. Tizen's WS service on :8002
    // can take 20–60 s to come up after a cold WoL, so a short budget races the
    // TV's boot.
    TvController tv;
    bool tokenChanged = false;
    String newToken;
    tv.configure(d_.config->tvIp, d_.config->tvToken,
                 [&](const String& t){ tokenChanged = true; newToken = t; });
    if (!tv.connectWithRetry(60000)) {
        Serial.println("[seq] tv ws failed");
        d_.led->setState(LedState::Error);
        // continue cleanup
    } else {
        // 5. KEY_HDMI3 (works on all Tizen 2018+ models). The spec mentions
        // a KEY_SOURCE+arrows fallback for older firmware; we hold off until
        // we know it's needed because the arrow count is TV-specific. If the
        // user reports KEY_HDMI3 doesn't switch inputs, add:
        //   tv.sendKey("KEY_SOURCE"); delay(800);
        //   for (int i = 0; i < N; i++) { tv.sendKey("KEY_RIGHT"); delay(150); }
        //   tv.sendKey("KEY_ENTER");
        // …with N tuned for that TV.
        // Settle the WS before the first key — some Tizen models close an
        // idle channel right after ms.channel.connect if no traffic arrives.
        tv.pump(200);
        // KEY_HDMI3 isn't recognized on every Tizen model. The robust recipe
        // is to open the source picker, mash LEFT to land on the leftmost
        // entry (TV), then RIGHT N times to reach HDMIn, then ENTER. The
        // menu opens at the *current* source, so we can't rely on the
        // starting position.
        const char* keys[] = {
            "KEY_SOURCE",
            "KEY_LEFT", "KEY_LEFT", "KEY_LEFT", "KEY_LEFT", "KEY_LEFT", "KEY_LEFT",
            "KEY_RIGHT", "KEY_RIGHT", "KEY_RIGHT", // TV → HDMI1 → HDMI2 → HDMI3
            "KEY_ENTER",
        };
        for (auto* k : keys) {
            tv.sendKey(k);
            tv.pump(250);
        }
        tv.pump(300);
        tv.disconnect();
    }

    // Persist token if it changed
    if (tokenChanged) {
        d_.config->tvToken = newToken;
        d_.config->save();
    }

    // 6. Stay on Fritzbox (idle network) — already there, nothing to do.

    d_.led->setState(LedState::Success);
    return true;
}
