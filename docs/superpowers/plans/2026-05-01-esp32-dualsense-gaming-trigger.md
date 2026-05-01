# ESP32 DualSense Gaming-mode Trigger — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ESP32-C3 firmware that watches BLE for a DualSense controller, then on detection wakes the gaming PC (WoL on TP-Link subnet) and powers on the Samsung Tizen TV with HDMI-3 selected (WoL + WebSocket on Fritzbox subnet).

**Architecture:** ESP32 idles on TP-Link WiFi running a passive BLE scanner. On DualSense MAC match, it broadcasts a WoL magic packet to the PC, hops to Fritzbox WiFi, broadcasts a WoL packet to the TV, opens a Tizen WebSocket, sends `KEY_HDMI3`, then hops back to TP-Link and resumes scanning. First-run config is collected via a SoftAP captive web page.

**Tech Stack:** Arduino framework, PlatformIO, ESP32-C3 Super Mini board (`esp32-c3-devkitm-1`). Libraries: NimBLE-Arduino (BLE scan), WiFi.h + WiFiUDP + WiFiClientSecure (WiFi, WoL, TLS), arduinoWebSockets (Tizen WebSocket), ArduinoJson (TV protocol + setup form), ESPAsyncWebServer + AsyncTCP (HTTP server), Preferences (NVS).

**Reference spec:** `docs/superpowers/specs/2026-05-01-esp32-dualsense-gaming-trigger-design.md`

---

## File Structure

```
esp32-tv/
├── platformio.ini                 # board, framework, libs, native test env
├── .gitignore                     # ignore .pio/, .vscode/, secrets
├── src/
│   ├── main.cpp                   # boot + dispatch (setup vs runtime)
│   ├── Config.h / .cpp            # NVS-backed config (load/save/has-all)
│   ├── StatusLed.h / .cpp         # non-blocking LED state machine
│   ├── Wol.h / .cpp               # magic packet builder + UDP sender
│   ├── NetworkManager.h / .cpp    # WiFi connect/disconnect/hop
│   ├── BleScanner.h / .cpp        # NimBLE scan + DualSense match
│   ├── TvController.h / .cpp      # Tizen WebSocket: pair, send key
│   ├── Sequence.h / .cpp          # the trigger sequence orchestrator
│   ├── SetupServer.h / .cpp       # AsyncWebServer for first-run setup
│   └── secrets_example.h          # template for local-only WiFi creds (dev)
└── test/
    └── test_native/               # pio test -e native
        ├── test_wol_packet.cpp    # magic packet builder
        └── test_status_led.cpp    # LED state machine
```

Each module is a small class with a narrow public interface, no globals. `main.cpp` is the only file that wires modules together.

---

## Task 1: Project scaffold + LED smoke test

**Goal:** Establish the PlatformIO project, get a flashable binary that proves the toolchain and board work end-to-end before adding features.

**Files:**
- Create: `platformio.ini`
- Create: `.gitignore`
- Create: `src/main.cpp`

- [ ] **Step 1: Write `platformio.ini`**

```ini
[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags =
  -D ARDUINO_USB_MODE=1
  -D ARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
  h2zero/NimBLE-Arduino@^2.0.0
  links2004/WebSockets@^2.4.1
  bblanchon/ArduinoJson@^7.0.4
  ESP32Async/ESPAsyncWebServer
  ESP32Async/AsyncTCP

[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17
```

- [ ] **Step 2: Write `.gitignore`**

```gitignore
.pio/
.vscode/
.DS_Store
src/secrets.h
*.bin
*.elf
```

- [ ] **Step 3: Write minimal `src/main.cpp` (LED blink on GPIO 8, active LOW)**

```cpp
#include <Arduino.h>

constexpr int LED_PIN = 8;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] esp32-tv smoke test");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // off (active LOW)
}

void loop() {
  digitalWrite(LED_PIN, LOW);
  delay(500);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  Serial.println("[loop] tick");
}
```

- [ ] **Step 4: Build**

Run: `pio run -e esp32c3`
Expected: clean build, no warnings about missing libs.

- [ ] **Step 5: Upload + verify on hardware**

Plug ESP32-C3 Super Mini via USB-C.
Run: `pio run -e esp32c3 -t upload`
Then: `pio device monitor`
Expected: serial output `[boot] esp32-tv smoke test`, then `[loop] tick` once per second; onboard LED blinking at 1 Hz. If LED is inverted (constantly on, brief off), confirm GPIO 8 active-LOW.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini .gitignore src/main.cpp
git commit -m "feat: project scaffold with LED smoke test"
```

---

## Task 2: Status LED state machine (with native test)

**Goal:** Drive the onboard LED to indicate state (setup / idle / running / success / error) without blocking the main loop. Pure-logic state-machine tested natively; only the GPIO write happens on-device.

**Files:**
- Create: `src/StatusLed.h`
- Create: `src/StatusLed.cpp`
- Create: `test/test_native/test_status_led.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Write the failing native test**

`test/test_native/test_status_led.cpp`:

```cpp
#include <unity.h>
#include "StatusLed.h"

void test_idle_heartbeat_brief_blip_every_3s() {
    StatusLed led;
    led.setState(LedState::Idle);
    // At t=0, blip starts
    TEST_ASSERT_TRUE(led.isOnAt(0));
    // 100ms blip width
    TEST_ASSERT_TRUE(led.isOnAt(99));
    TEST_ASSERT_FALSE(led.isOnAt(101));
    // Off for the rest of the 3s period
    TEST_ASSERT_FALSE(led.isOnAt(2999));
    // Next blip
    TEST_ASSERT_TRUE(led.isOnAt(3000));
}

void test_running_fast_blink_5hz() {
    StatusLed led;
    led.setState(LedState::Running);
    // Period 200ms, on for first 100ms, off for second 100ms
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(99));
    TEST_ASSERT_FALSE(led.isOnAt(100));
    TEST_ASSERT_FALSE(led.isOnAt(199));
    TEST_ASSERT_TRUE(led.isOnAt(200));
}

void test_success_solid_for_2s_then_idle() {
    StatusLed led;
    led.setState(LedState::Success); // entered at t=0
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(1999));
    // After 2000ms, transitions to Idle automatically
    led.tick(2001);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

void test_setup_slow_1hz_blink() {
    StatusLed led;
    led.setState(LedState::Setup);
    // 500ms on, 500ms off
    TEST_ASSERT_TRUE(led.isOnAt(0));
    TEST_ASSERT_TRUE(led.isOnAt(499));
    TEST_ASSERT_FALSE(led.isOnAt(500));
    TEST_ASSERT_FALSE(led.isOnAt(999));
    TEST_ASSERT_TRUE(led.isOnAt(1000));
}

void test_error_three_pulses_then_idle() {
    StatusLed led;
    led.setState(LedState::Error); // entered at t=0
    // Three 100ms pulses with 100ms gaps: on 0-100, off 100-200, on 200-300, off 300-400, on 400-500, off 500-600
    TEST_ASSERT_TRUE(led.isOnAt(50));
    TEST_ASSERT_FALSE(led.isOnAt(150));
    TEST_ASSERT_TRUE(led.isOnAt(250));
    TEST_ASSERT_FALSE(led.isOnAt(350));
    TEST_ASSERT_TRUE(led.isOnAt(450));
    TEST_ASSERT_FALSE(led.isOnAt(550));
    led.tick(601);
    TEST_ASSERT_EQUAL(LedState::Idle, led.state());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_heartbeat_brief_blip_every_3s);
    RUN_TEST(test_running_fast_blink_5hz);
    RUN_TEST(test_success_solid_for_2s_then_idle);
    RUN_TEST(test_setup_slow_1hz_blink);
    RUN_TEST(test_error_three_pulses_then_idle);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test (must fail because StatusLed doesn't exist yet)**

Run: `pio test -e native`
Expected: compile error — `StatusLed.h: No such file or directory`.

- [ ] **Step 3: Write `src/StatusLed.h`**

```cpp
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
```

- [ ] **Step 4: Write `src/StatusLed.cpp`**

```cpp
#include "StatusLed.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

StatusLed::StatusLed() : state_(LedState::Setup), state_entered_at_(0) {}

void StatusLed::setState(LedState s) {
    state_ = s;
    state_entered_at_ = 0;
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
    if (state_entered_at_ == 0) state_entered_at_ = (now_ms == 0) ? 1 : now_ms;
    uint32_t elapsed = now_ms - state_entered_at_;

    if (state_ == LedState::Success && elapsed >= 2000) {
        setState(LedState::Idle);
        state_entered_at_ = (now_ms == 0) ? 1 : now_ms;
        elapsed = 0;
    } else if (state_ == LedState::Error && elapsed >= 600) {
        setState(LedState::Idle);
        state_entered_at_ = (now_ms == 0) ? 1 : now_ms;
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
```

- [ ] **Step 5: Run the test, must pass**

Run: `pio test -e native`
Expected:
```
test_idle_heartbeat_brief_blip_every_3s    PASSED
test_running_fast_blink_5hz                PASSED
test_success_solid_for_2s_then_idle        PASSED
test_setup_slow_1hz_blink                  PASSED
test_error_three_pulses_then_idle          PASSED
5 Tests 0 Failures 0 Ignored
```

- [ ] **Step 6: Wire into `src/main.cpp` for on-device sanity**

Replace `src/main.cpp` with:

```cpp
#include <Arduino.h>
#include "StatusLed.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] led demo");
  led.attachPin(LED_PIN);
  led.setState(LedState::Idle);
}

void loop() {
  led.tick(millis());
  delay(10);
}
```

- [ ] **Step 7: Upload and verify visually**

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected: a brief blip every 3 s on the onboard LED.

- [ ] **Step 8: Commit**

```bash
git add src/StatusLed.h src/StatusLed.cpp test/test_native/test_status_led.cpp src/main.cpp platformio.ini
git commit -m "feat: status LED state machine with native tests"
```

---

## Task 3: WoL magic packet builder (pure function, native test)

**Goal:** A tested pure-logic builder that converts a MAC string into the 102-byte magic packet payload.

**Files:**
- Create: `src/Wol.h`
- Create: `src/Wol.cpp`
- Create: `test/test_native/test_wol_packet.cpp`

- [ ] **Step 1: Write the failing native test**

`test/test_native/test_wol_packet.cpp`:

```cpp
#include <unity.h>
#include "Wol.h"
#include <string.h>

void test_parse_mac_colon_separated() {
    uint8_t mac[6];
    TEST_ASSERT_TRUE(Wol::parseMac("AA:BB:CC:11:22:33", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, mac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, mac[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, mac[3]);
    TEST_ASSERT_EQUAL_HEX8(0x22, mac[4]);
    TEST_ASSERT_EQUAL_HEX8(0x33, mac[5]);
}

void test_parse_mac_dash_separated() {
    uint8_t mac[6];
    TEST_ASSERT_TRUE(Wol::parseMac("aa-bb-cc-11-22-33", mac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0x33, mac[5]);
}

void test_parse_mac_invalid_returns_false() {
    uint8_t mac[6];
    TEST_ASSERT_FALSE(Wol::parseMac("not-a-mac", mac));
    TEST_ASSERT_FALSE(Wol::parseMac("AA:BB:CC:11:22", mac));        // too short
    TEST_ASSERT_FALSE(Wol::parseMac("ZZ:BB:CC:11:22:33", mac));     // bad hex
}

void test_magic_packet_is_102_bytes() {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    uint8_t pkt[102];
    Wol::buildMagicPacket(mac, pkt);
    // First 6 bytes are 0xFF
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, pkt[i]);
    }
    // Next 96 bytes are the MAC repeated 16 times
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < 6; i++) {
            TEST_ASSERT_EQUAL_HEX8(mac[i], pkt[6 + rep * 6 + i]);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_mac_colon_separated);
    RUN_TEST(test_parse_mac_dash_separated);
    RUN_TEST(test_parse_mac_invalid_returns_false);
    RUN_TEST(test_magic_packet_is_102_bytes);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test, must fail**

Run: `pio test -e native -f test_wol_packet`
Expected: compile error — `Wol.h: No such file or directory`.

- [ ] **Step 3: Write `src/Wol.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Wol {
    // Parses "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF" (case-insensitive).
    // Returns true on success and fills out[0..5].
    bool parseMac(const char* mac_str, uint8_t out[6]);

    // Writes 102 bytes into out: 6 * 0xFF then mac repeated 16 times.
    void buildMagicPacket(const uint8_t mac[6], uint8_t out[102]);

#ifdef ARDUINO
    // Sends the magic packet as a UDP broadcast on port 9.
    // Caller must be connected to a WiFi network. Returns true if the
    // packet was handed to the stack.
    bool sendBroadcast(const char* mac_str);
#endif
}
```

- [ ] **Step 4: Write `src/Wol.cpp`**

```cpp
#include "Wol.h"
#include <string.h>
#include <ctype.h>

#ifdef ARDUINO
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

namespace {
    int hexNibble(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        c = (char)tolower((unsigned char)c);
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return -1;
    }
}

namespace Wol {

bool parseMac(const char* mac_str, uint8_t out[6]) {
    if (!mac_str) return false;
    // Expect 17 chars: 6 hex pairs separated by ':' or '-'
    if (strlen(mac_str) != 17) return false;
    for (int i = 0; i < 6; i++) {
        int hi = hexNibble(mac_str[i * 3]);
        int lo = hexNibble(mac_str[i * 3 + 1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5) {
            char sep = mac_str[i * 3 + 2];
            if (sep != ':' && sep != '-') return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void buildMagicPacket(const uint8_t mac[6], uint8_t out[102]) {
    for (int i = 0; i < 6; i++) out[i] = 0xFF;
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < 6; i++) {
            out[6 + rep * 6 + i] = mac[i];
        }
    }
}

#ifdef ARDUINO
bool sendBroadcast(const char* mac_str) {
    uint8_t mac[6];
    if (!parseMac(mac_str, mac)) return false;
    uint8_t pkt[102];
    buildMagicPacket(mac, pkt);

    WiFiUDP udp;
    if (!udp.beginPacket(IPAddress(255, 255, 255, 255), 9)) return false;
    udp.write(pkt, sizeof(pkt));
    return udp.endPacket() == 1;
}
#endif

}
```

- [ ] **Step 5: Run the test, must pass**

Run: `pio test -e native -f test_wol_packet`
Expected: `4 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Commit**

```bash
git add src/Wol.h src/Wol.cpp test/test_native/test_wol_packet.cpp
git commit -m "feat: WoL magic packet builder + UDP broadcast"
```

---

## Task 4: Config storage (NVS via Preferences)

**Goal:** Persist config (two WiFi credential pairs, PC MAC, TV IP+MAC+token, DualSense MAC) across reboots, with a single `Config::hasAll()` predicate that determines setup-mode vs runtime.

**Files:**
- Create: `src/Config.h`
- Create: `src/Config.cpp`

NVS I/O is ESP32-only and trusted upstream; this task is wiring + on-device verification rather than TDD.

- [ ] **Step 1: Write `src/Config.h`**

```cpp
#pragma once
#include <Arduino.h>

struct Config {
    String tplinkSsid;
    String tplinkPass;
    String fritzboxSsid;
    String fritzboxPass;
    String pcMac;          // "AA:BB:CC:DD:EE:FF"
    String tvIp;           // "192.168.178.42"
    String tvMac;
    String tvToken;        // empty until pairing succeeds
    String dualsenseMac;

    static Config load();
    void save() const;
    static void clear();

    // Has every value needed to enter runtime mode.
    bool hasAll() const {
        return tplinkSsid.length() && tplinkPass.length()
            && fritzboxSsid.length() && fritzboxPass.length()
            && pcMac.length()
            && tvIp.length() && tvMac.length()
            && dualsenseMac.length();
        // tvToken is allowed to be empty; the first runtime sequence will
        // attempt to pair, but the user generally pairs explicitly during setup.
    }
};
```

- [ ] **Step 2: Write `src/Config.cpp`**

```cpp
#include "Config.h"
#include <Preferences.h>

namespace {
    constexpr const char* NS = "esp32tv";
    Preferences& prefs() {
        static Preferences p;
        return p;
    }
}

Config Config::load() {
    Config c;
    prefs().begin(NS, true); // read-only
    c.tplinkSsid    = prefs().getString("tpSsid", "");
    c.tplinkPass    = prefs().getString("tpPass", "");
    c.fritzboxSsid  = prefs().getString("fbSsid", "");
    c.fritzboxPass  = prefs().getString("fbPass", "");
    c.pcMac         = prefs().getString("pcMac", "");
    c.tvIp          = prefs().getString("tvIp", "");
    c.tvMac         = prefs().getString("tvMac", "");
    c.tvToken       = prefs().getString("tvTok", "");
    c.dualsenseMac  = prefs().getString("dsMac", "");
    prefs().end();
    return c;
}

void Config::save() const {
    prefs().begin(NS, false); // read-write
    prefs().putString("tpSsid", tplinkSsid);
    prefs().putString("tpPass", tplinkPass);
    prefs().putString("fbSsid", fritzboxSsid);
    prefs().putString("fbPass", fritzboxPass);
    prefs().putString("pcMac", pcMac);
    prefs().putString("tvIp", tvIp);
    prefs().putString("tvMac", tvMac);
    prefs().putString("tvTok", tvToken);
    prefs().putString("dsMac", dualsenseMac);
    prefs().end();
}

void Config::clear() {
    prefs().begin(NS, false);
    prefs().clear();
    prefs().end();
}
```

- [ ] **Step 3: On-device smoke test by extending `src/main.cpp`**

Replace `src/main.cpp` with:

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] config demo");
  led.attachPin(LED_PIN);

  Config c = Config::load();
  Serial.printf("[cfg] hasAll=%d  tplinkSsid='%s'  pcMac='%s'  tvIp='%s'\n",
                c.hasAll(), c.tplinkSsid.c_str(), c.pcMac.c_str(), c.tvIp.c_str());

  led.setState(c.hasAll() ? LedState::Idle : LedState::Setup);
}

void loop() {
  led.tick(millis());
  delay(10);
}
```

- [ ] **Step 4: Upload and verify**

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected: first boot prints `hasAll=0` (no config) and LED enters slow setup blink. Reset board → same output (NVS is empty until we write).

- [ ] **Step 5: Commit**

```bash
git add src/Config.h src/Config.cpp src/main.cpp
git commit -m "feat: Config persisted via NVS Preferences"
```

---

## Task 5: Network manager (WiFi connect / disconnect / hop)

**Goal:** A single class that owns WiFi state and exposes blocking `connectTplink()` / `connectFritzbox()` / `disconnect()` / `hopTo(...)` calls, each with a timeout and clear success/failure return.

**Files:**
- Create: `src/NetworkManager.h`
- Create: `src/NetworkManager.cpp`
- Modify: `src/main.cpp` (smoke test)

WiFi behavior cannot be host-tested meaningfully; verify on-device.

- [ ] **Step 1: Write `src/NetworkManager.h`**

```cpp
#pragma once
#include <Arduino.h>

enum class WifiTarget { None, TpLink, Fritzbox };

class NetworkManager {
public:
    void configure(const String& tpSsid, const String& tpPass,
                   const String& fbSsid, const String& fbPass);

    // Blocks up to timeout_ms. Returns true on success.
    bool connect(WifiTarget target, uint32_t timeout_ms = 15000);
    void disconnect();
    WifiTarget current() const { return current_; }

    // Convenience wrappers:
    bool connectTplink(uint32_t t = 15000)    { return connect(WifiTarget::TpLink, t); }
    bool connectFritzbox(uint32_t t = 15000)  { return connect(WifiTarget::Fritzbox, t); }

    // Disconnect + connect to a different target. Returns true on success.
    bool hopTo(WifiTarget target, uint32_t timeout_ms = 15000);

    String localIp() const;

private:
    String tpSsid_, tpPass_, fbSsid_, fbPass_;
    WifiTarget current_ = WifiTarget::None;
};
```

- [ ] **Step 2: Write `src/NetworkManager.cpp`**

```cpp
#include "NetworkManager.h"
#include <WiFi.h>

void NetworkManager::configure(const String& tpSsid, const String& tpPass,
                               const String& fbSsid, const String& fbPass) {
    tpSsid_ = tpSsid; tpPass_ = tpPass;
    fbSsid_ = fbSsid; fbPass_ = fbPass;
}

bool NetworkManager::connect(WifiTarget target, uint32_t timeout_ms) {
    if (current_ == target && WiFi.isConnected()) return true;
    if (WiFi.isConnected()) WiFi.disconnect(true, true);

    const char* ssid = nullptr;
    const char* pass = nullptr;
    if (target == WifiTarget::TpLink) {
        ssid = tpSsid_.c_str(); pass = tpPass_.c_str();
    } else if (target == WifiTarget::Fritzbox) {
        ssid = fbSsid_.c_str(); pass = fbPass_.c_str();
    } else {
        return false;
    }

    Serial.printf("[net] connecting to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (WiFi.isConnected()) {
            current_ = target;
            Serial.printf("[net] connected ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(100);
    }
    Serial.printf("[net] timeout connecting to %s\n", ssid);
    WiFi.disconnect(true, true);
    current_ = WifiTarget::None;
    return false;
}

void NetworkManager::disconnect() {
    if (WiFi.isConnected()) WiFi.disconnect(true, true);
    current_ = WifiTarget::None;
}

bool NetworkManager::hopTo(WifiTarget target, uint32_t timeout_ms) {
    disconnect();
    delay(200);
    return connect(target, timeout_ms);
}

String NetworkManager::localIp() const {
    return WiFi.isConnected() ? WiFi.localIP().toString() : String("");
}
```

- [ ] **Step 3: Manual smoke-test path via `src/main.cpp`**

Temporarily hard-code your real SSIDs into `src/secrets.h` (gitignored). Create `src/secrets_example.h`:

```cpp
#pragma once
#define SECRET_TPLINK_SSID "your-tplink-ssid"
#define SECRET_TPLINK_PASS "your-tplink-pass"
#define SECRET_FRITZBOX_SSID "your-fritzbox-ssid"
#define SECRET_FRITZBOX_PASS "your-fritzbox-pass"
```

Then `src/secrets.h` (gitignored) with real values. Replace `src/main.cpp` with:

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "NetworkManager.h"
#include "secrets.h"

constexpr int LED_PIN = 8;
StatusLed led;
NetworkManager net;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);
  led.setState(LedState::Running);
  net.configure(SECRET_TPLINK_SSID, SECRET_TPLINK_PASS,
                SECRET_FRITZBOX_SSID, SECRET_FRITZBOX_PASS);

  Serial.println("[demo] connect to tplink");
  bool ok1 = net.connectTplink();
  Serial.printf("[demo] tplink ok=%d ip=%s\n", ok1, net.localIp().c_str());
  delay(2000);

  Serial.println("[demo] hop to fritzbox");
  bool ok2 = net.hopTo(WifiTarget::Fritzbox);
  Serial.printf("[demo] fritzbox ok=%d ip=%s\n", ok2, net.localIp().c_str());
  delay(2000);

  Serial.println("[demo] hop back to tplink");
  bool ok3 = net.hopTo(WifiTarget::TpLink);
  Serial.printf("[demo] tplink ok=%d ip=%s\n", ok3, net.localIp().c_str());

  led.setState(LedState::Idle);
}

void loop() { led.tick(millis()); delay(10); }
```

- [ ] **Step 4: Upload and verify**

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected: three connect events, each printing a different IP (TP-Link subnet, Fritzbox subnet, TP-Link subnet). Each connect should complete in 3–10 s.

- [ ] **Step 5: Remove the demo code from `main.cpp`**

Restore `src/main.cpp` to a placeholder; we'll wire properly in Task 11. Replace with:

```cpp
#include <Arduino.h>
#include "StatusLed.h"

constexpr int LED_PIN = 8;
StatusLed led;

void setup() { Serial.begin(115200); led.attachPin(LED_PIN); led.setState(LedState::Idle); }
void loop()  { led.tick(millis()); delay(10); }
```

- [ ] **Step 6: Commit**

```bash
git add src/NetworkManager.h src/NetworkManager.cpp src/secrets_example.h src/main.cpp
git commit -m "feat: NetworkManager with WiFi hop"
```

---

## Task 6: BLE scanner with DualSense filter

**Goal:** Continuously scan BLE; on each advertisement that looks like a DualSense, invoke a callback with the MAC and RSSI. Filter by Sony manufacturer ID `0x054C` and/or device name `Wireless Controller`.

**Files:**
- Create: `src/BleScanner.h`
- Create: `src/BleScanner.cpp`
- Modify: `src/main.cpp` (smoke test)

NimBLE's API differs by version. The code below targets NimBLE-Arduino 2.x.

- [ ] **Step 1: Write `src/BleScanner.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <functional>

struct BleHit {
    String mac;     // lowercase "aa:bb:cc:dd:ee:ff"
    int rssi;
    String name;
};

class BleScanner {
public:
    using HitCallback = std::function<void(const BleHit&)>;

    void begin();
    void onHit(HitCallback cb) { cb_ = cb; }
    void start(uint32_t duration_ms = 0); // 0 = continuous
    void stop();

    // Called by the NimBLE callback shim — public for that reason, not for users.
    void onHitInternal(const BleHit& h);

    // Match predicate: any advertiser whose name contains "Wireless Controller"
    // OR whose manufacturer data starts with Sony's company ID (0x4C 0x00, little-endian).
    static bool looksLikeDualSense(const String& name, const uint8_t* mfg, size_t mfg_len);

private:
    HitCallback cb_;
    bool started_ = false;
};
```

- [ ] **Step 2: Write `src/BleScanner.cpp`**

```cpp
#include "BleScanner.h"
#include <NimBLEDevice.h>

namespace {
    BleScanner* g_self = nullptr;

    class ScanCallbacks : public NimBLEScanCallbacks {
        void onResult(const NimBLEAdvertisedDevice* dev) override {
            if (!g_self) return;
            String name = String(dev->getName().c_str());
            const uint8_t* mfg = nullptr;
            size_t mfg_len = 0;
            if (dev->haveManufacturerData()) {
                auto data = dev->getManufacturerData();
                mfg = (const uint8_t*)data.data();
                mfg_len = data.size();
            }
            if (!BleScanner::looksLikeDualSense(name, mfg, mfg_len)) return;

            BleHit hit;
            hit.mac = String(dev->getAddress().toString().c_str());
            hit.mac.toLowerCase();
            hit.rssi = dev->getRSSI();
            hit.name = name;
            g_self->onHitInternal(hit);
        }
    };

    static ScanCallbacks s_cb;
}

void BleScanner::begin() {
    g_self = this;
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    auto* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_cb, false);
    scan->setActiveScan(false);   // passive — don't probe
    scan->setInterval(100);       // ms
    scan->setWindow(50);          // ms
}

void BleScanner::start(uint32_t duration_ms) {
    if (started_) return;
    auto* scan = NimBLEDevice::getScan();
    scan->start(duration_ms / 1000, false, true); // duration in seconds; 0=continuous
    started_ = true;
}

void BleScanner::stop() {
    if (!started_) return;
    NimBLEDevice::getScan()->stop();
    started_ = false;
}

void BleScanner::onHitInternal(const BleHit& h) { if (cb_) cb_(h); }

bool BleScanner::looksLikeDualSense(const String& name, const uint8_t* mfg, size_t mfg_len) {
    if (name.indexOf("Wireless Controller") >= 0) return true;
    if (mfg && mfg_len >= 2) {
        // Sony Corp company ID 0x054C, little-endian on the wire = 0x4C 0x05
        if (mfg[0] == 0x4C && mfg[1] == 0x05) return true;
    }
    return false;
}
```

- [ ] **Step 3: Smoke-test on device by replacing `src/main.cpp`**

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "BleScanner.h"

constexpr int LED_PIN = 8;
StatusLed led;
BleScanner scanner;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);
  led.setState(LedState::Idle);

  scanner.begin();
  scanner.onHit([](const BleHit& h){
    Serial.printf("[ble] hit mac=%s rssi=%d name='%s'\n",
                  h.mac.c_str(), h.rssi, h.name.c_str());
  });
  scanner.start(0);
}

void loop() { led.tick(millis()); delay(10); }
```

- [ ] **Step 4: Upload and verify**

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Power your DualSense on (PS button). Within ~1 s, expect a line like:
```
[ble] hit mac=aa:bb:cc:11:22:33 rssi=-45 name='Wireless Controller'
```
Note the MAC for use during the real pairing flow later.

- [ ] **Step 5: Commit**

```bash
git add src/BleScanner.h src/BleScanner.cpp src/main.cpp
git commit -m "feat: BLE scanner with DualSense filter"
```

---

## Task 7: Tizen TV WebSocket client

**Goal:** Connect to the Samsung TV's WebSocket endpoint, complete the one-time pairing handshake, store the token, and send key commands (`KEY_HDMI3` etc.).

**Files:**
- Create: `src/TvController.h`
- Create: `src/TvController.cpp`
- Modify: `src/main.cpp` (smoke test)

Tizen self-signs its TLS cert; we use `WiFiClientSecure::setInsecure()`. Library: `WebSocketsClient` from `links2004/WebSockets`.

- [ ] **Step 1: Write `src/TvController.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>

class TvController {
public:
    // ip: TV's IP. token: empty for first pairing, otherwise saved token.
    // onTokenIssued: called when the TV provides a fresh token (during pairing).
    using TokenCallback = std::function<void(const String&)>;

    void configure(const String& ip, const String& token, TokenCallback onToken);

    // Connect with retries up to total_timeout_ms; each attempt has its own short timeout.
    bool connectWithRetry(uint32_t total_timeout_ms = 15000);

    // Send a single Tizen remote-control key. Returns true if the websocket
    // was open and the message was queued.
    bool sendKey(const char* key);

    void disconnect();

private:
    WebSocketsClient ws_;
    String ip_;
    String token_;
    TokenCallback onToken_;
    bool connected_ = false;

    void onEvent(WStype_t type, uint8_t* payload, size_t len);
    static void onEventStatic(WStype_t type, uint8_t* payload, size_t len);
    static TvController* s_instance_;

    String buildPath() const;
};
```

- [ ] **Step 2: Write `src/TvController.cpp`**

```cpp
#include "TvController.h"
#include <ArduinoJson.h>
#include <base64.h>

TvController* TvController::s_instance_ = nullptr;

void TvController::configure(const String& ip, const String& token, TokenCallback onToken) {
    ip_ = ip;
    token_ = token;
    onToken_ = onToken;
}

String TvController::buildPath() const {
    // name must be base64-encoded
    String name = base64::encode("esp32-tv");
    String path = "/api/v2/channels/samsung.remote.control?name=" + name;
    if (token_.length()) path += "&token=" + token_;
    return path;
}

bool TvController::connectWithRetry(uint32_t total_timeout_ms) {
    s_instance_ = this;
    ws_.beginSslWithCA(ip_.c_str(), 8002, buildPath().c_str(), nullptr, "");
    ws_.setInsecure();
    ws_.onEvent(&TvController::onEventStatic);
    ws_.setReconnectInterval(1000);

    uint32_t start = millis();
    while (millis() - start < total_timeout_ms) {
        ws_.loop();
        if (connected_) return true;
        delay(50);
    }
    return false;
}

bool TvController::sendKey(const char* key) {
    if (!connected_) return false;
    JsonDocument doc;
    doc["method"] = "ms.remote.control";
    auto params = doc["params"].to<JsonObject>();
    params["Cmd"] = "Click";
    params["DataOfCmd"] = key;
    params["Option"] = "false";
    params["TypeOfRemote"] = "SendRemoteKey";
    String out;
    serializeJson(doc, out);
    return ws_.sendTXT(out);
}

void TvController::disconnect() {
    ws_.disconnect();
    connected_ = false;
}

void TvController::onEventStatic(WStype_t type, uint8_t* payload, size_t len) {
    if (s_instance_) s_instance_->onEvent(type, payload, len);
}

void TvController::onEvent(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.printf("[tv] connected path=%s\n", buildPath().c_str());
            break;
        case WStype_DISCONNECTED:
            Serial.println("[tv] disconnected");
            connected_ = false;
            break;
        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, len);
            if (err) {
                Serial.printf("[tv] bad json: %s\n", err.c_str());
                return;
            }
            const char* event = doc["event"] | "";
            Serial.printf("[tv] event=%s\n", event);
            if (strcmp(event, "ms.channel.connect") == 0) {
                connected_ = true;
                const char* tok = doc["data"]["token"] | "";
                if (tok && *tok && onToken_) {
                    String t(tok);
                    if (t != token_) {
                        token_ = t;
                        onToken_(t);
                    }
                }
            } else if (strcmp(event, "ms.channel.unauthorized") == 0) {
                Serial.println("[tv] unauthorized — accept the prompt on the TV remote");
            }
            break;
        }
        default: break;
    }
}
```

- [ ] **Step 3: Smoke-test on device**

Replace `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "NetworkManager.h"
#include "TvController.h"
#include "secrets.h"

// Add to secrets.h: SECRET_TV_IP "192.168.178.42"
constexpr int LED_PIN = 8;
StatusLed led;
NetworkManager net;
TvController tv;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);
  led.setState(LedState::Running);

  net.configure(SECRET_TPLINK_SSID, SECRET_TPLINK_PASS,
                SECRET_FRITZBOX_SSID, SECRET_FRITZBOX_PASS);
  if (!net.connectFritzbox()) {
    Serial.println("[demo] WiFi failed"); led.setState(LedState::Error); return;
  }

  tv.configure(SECRET_TV_IP, "", [](const String& token){
    Serial.printf("[demo] got token: %s\n", token.c_str());
  });
  if (!tv.connectWithRetry(15000)) {
    Serial.println("[demo] tv connect failed"); led.setState(LedState::Error); return;
  }
  delay(500);
  Serial.println("[demo] sending KEY_HDMI3");
  tv.sendKey("KEY_HDMI3");
  delay(500);
  led.setState(LedState::Success);
}

void loop() { led.tick(millis()); delay(10); }
```

- [ ] **Step 4: First-run pairing**

Make sure the TV is **on** for this initial pairing. Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected serial flow:
```
[net] connected ip=...
[tv] connected path=/api/v2/channels/...
[tv] event=ms.channel.unauthorized        <-- TV is showing prompt now
```
A prompt appears on the TV; accept it with the remote.
```
[tv] event=ms.channel.connect
[demo] got token: <some-long-string>
[demo] sending KEY_HDMI3
```
The TV should switch to HDMI 3.

- [ ] **Step 5: Save the token manually for the next test**

Copy the token from serial output and paste it into `secrets.h`:
```cpp
#define SECRET_TV_TOKEN "<token-from-serial>"
```
Then change `tv.configure(SECRET_TV_IP, "", ...)` → `tv.configure(SECRET_TV_IP, SECRET_TV_TOKEN, ...)`. Re-flash. This time no prompt should appear; HDMI 3 should be selected directly.

- [ ] **Step 6: Restore main.cpp to placeholder**

Same as the placeholder at the end of Task 5.

- [ ] **Step 7: Commit**

```bash
git add src/TvController.h src/TvController.cpp src/main.cpp
git commit -m "feat: Tizen WebSocket client with pairing"
```

---

## Task 8: Sequence orchestrator

**Goal:** A single `Sequence::run()` method that executes the full WoL → hop → WoL TV → WebSocket → KEY_HDMI3 → reset flow, driving the LED through Running → Success/Error.

**Files:**
- Create: `src/Sequence.h`
- Create: `src/Sequence.cpp`
- Modify: `src/main.cpp` (smoke test)

- [ ] **Step 1: Write `src/Sequence.h`**

```cpp
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
```

- [ ] **Step 2: Write `src/Sequence.cpp`**

```cpp
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

    // 4. WebSocket connect with retry up to 15 s
    TvController tv;
    bool tokenChanged = false;
    String newToken;
    tv.configure(d_.config->tvIp, d_.config->tvToken,
                 [&](const String& t){ tokenChanged = true; newToken = t; });
    if (!tv.connectWithRetry(15000)) {
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
        tv.sendKey("KEY_HDMI3");
        delay(500); // give TV a moment to process
        tv.disconnect();
    }

    // Persist token if it changed
    if (tokenChanged) {
        d_.config->tvToken = newToken;
        d_.config->save();
    }

    // 6. Hop back to TP-Link
    if (!d_.net->hopTo(WifiTarget::TpLink)) {
        Serial.println("[seq] post-sequence tplink reconnect failed");
        d_.led->setState(LedState::Error);
        return false;
    }

    d_.led->setState(LedState::Success);
    return true;
}
```

- [ ] **Step 3: Smoke-test by manually triggering once at boot**

Replace `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "Sequence.h"
#include "secrets.h"

// For this test, populate Config in code from secrets.h instead of NVS.
constexpr int LED_PIN = 8;
StatusLed led;
NetworkManager net;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);

  Config c;
  c.tplinkSsid = SECRET_TPLINK_SSID; c.tplinkPass = SECRET_TPLINK_PASS;
  c.fritzboxSsid = SECRET_FRITZBOX_SSID; c.fritzboxPass = SECRET_FRITZBOX_PASS;
  c.pcMac = SECRET_PC_MAC;
  c.tvIp = SECRET_TV_IP; c.tvMac = SECRET_TV_MAC; c.tvToken = SECRET_TV_TOKEN;
  c.dualsenseMac = "00:00:00:00:00:00"; // unused for this test

  net.configure(c.tplinkSsid, c.tplinkPass, c.fritzboxSsid, c.fritzboxPass);
  if (!net.connectTplink()) { led.setState(LedState::Error); return; }

  Sequence seq({ &c, &net, &led });
  seq.run();
}

void loop() { led.tick(millis()); delay(10); }
```

Add to `secrets.h`:
```cpp
#define SECRET_PC_MAC  "AA:BB:CC:DD:EE:FF"
#define SECRET_TV_MAC  "11:22:33:44:55:66"
```

- [ ] **Step 4: Verify on hardware**

Make sure: PC is off, TV is off, ESP32 is in BLE/WiFi range of both routers. WoL is enabled in PC BIOS and Samsung TV "Network Standby" / "Wake on LAN" is enabled.

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected order in log:
```
[net] connected ip=192.168.0.x
[seq] WoL→PC sent
[net] connected ip=192.168.178.x
[seq] WoL→TV sent
[tv] connected ...
[tv] event=ms.channel.connect
[seq] (no errors)
[net] connected ip=192.168.0.x
```
Visually: PC starts booting, TV powers on within ~5 s and lands on HDMI 3, LED goes solid for 2 s.

- [ ] **Step 5: Restore `main.cpp` placeholder**

- [ ] **Step 6: Commit**

```bash
git add src/Sequence.h src/Sequence.cpp src/main.cpp
git commit -m "feat: end-to-end gaming sequence orchestrator"
```

---

## Task 9: Setup web server (SoftAP mode)

**Goal:** When config is missing, ESP32 boots into SoftAP mode (`esp32-tv-setup`, no password) and serves a web form. Submitting the form populates Config, pairs the gamepad, pairs the TV, and reboots into runtime mode.

**Files:**
- Create: `src/SetupServer.h`
- Create: `src/SetupServer.cpp`
- Modify: `src/main.cpp` (Task 10 will fully wire; here we smoke-test setup mode)

- [ ] **Step 1: Write `src/SetupServer.h`**

```cpp
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "BleScanner.h"

class SetupServer {
public:
    SetupServer(Config& cfg, BleScanner& scanner) : cfg_(cfg), scanner_(scanner), server_(80) {}
    void begin();
private:
    Config& cfg_;
    BleScanner& scanner_;
    AsyncWebServer server_;

    // Pair-gamepad state
    String best_ds_mac_;
    int best_ds_rssi_ = -127;
    bool pairing_ = false;
    uint32_t pair_started_at_ = 0;

    void handleRoot(AsyncWebServerRequest* req);
    void handleSave(AsyncWebServerRequest* req);
    void handlePairStart(AsyncWebServerRequest* req);
    void handlePairStatus(AsyncWebServerRequest* req);
    void handleReset(AsyncWebServerRequest* req);
};
```

- [ ] **Step 2: Write `src/SetupServer.cpp`**

```cpp
#include "SetupServer.h"
#include <WiFi.h>
#include <ArduinoJson.h>

namespace {
    const char* INDEX_HTML = R"HTML(
<!doctype html><meta charset=utf-8><title>esp32-tv setup</title>
<style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}
input{width:100%;padding:.4em;margin:.2em 0;box-sizing:border-box}
button{padding:.5em 1em;margin:.4em 0}
fieldset{margin:1em 0}</style>
<h1>esp32-tv setup</h1>
<form method=post action=/save>
<fieldset><legend>WiFi: TP-Link (where the PC is reachable for WoL)</legend>
SSID <input name=tpSsid>
Password <input name=tpPass type=password>
</fieldset>
<fieldset><legend>WiFi: Fritzbox (where the TV is)</legend>
SSID <input name=fbSsid>
Password <input name=fbPass type=password>
</fieldset>
<fieldset><legend>Devices</legend>
PC's wired-NIC MAC <input name=pcMac placeholder="AA:BB:CC:DD:EE:FF">
Samsung TV IP <input name=tvIp placeholder="192.168.178.42">
Samsung TV MAC (WiFi) <input name=tvMac placeholder="AA:BB:CC:DD:EE:FF">
DualSense MAC <input name=dsMac id=dsMac placeholder="(use Pair button below)">
<button type=button onclick="startPair()">Pair gamepad</button>
<span id=pairStatus></span>
</fieldset>
<button type=submit>Save and reboot</button>
</form>
<script>
async function startPair(){
  document.getElementById('pairStatus').textContent='scanning 30s — power on the controller';
  await fetch('/pair-start', {method:'POST'});
  let started = Date.now();
  let timer = setInterval(async ()=>{
    let r = await fetch('/pair-status'); let j = await r.json();
    if (j.mac){
      document.getElementById('dsMac').value = j.mac;
      document.getElementById('pairStatus').textContent = 'paired (RSSI '+j.rssi+')';
      clearInterval(timer);
    } else if (Date.now() - started > 30000){
      document.getElementById('pairStatus').textContent = 'no controller seen';
      clearInterval(timer);
    }
  }, 1000);
}
</script>
)HTML";
}

void SetupServer::begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("esp32-tv-setup");
    Serial.printf("[setup] AP up at %s\n", WiFi.softAPIP().toString().c_str());

    server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* r){ handleRoot(r); });
    server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r){ handleSave(r); });
    server_.on("/pair-start", HTTP_POST, [this](AsyncWebServerRequest* r){ handlePairStart(r); });
    server_.on("/pair-status", HTTP_GET, [this](AsyncWebServerRequest* r){ handlePairStatus(r); });
    server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest* r){ handleReset(r); });
    server_.begin();

    scanner_.begin();
    scanner_.onHit([this](const BleHit& h){
        if (!pairing_) return;
        if (millis() - pair_started_at_ > 30000) { pairing_ = false; scanner_.stop(); return; }
        if (h.rssi > best_ds_rssi_) {
            best_ds_rssi_ = h.rssi;
            best_ds_mac_ = h.mac;
        }
    });
}

void SetupServer::handleRoot(AsyncWebServerRequest* req) {
    req->send(200, "text/html", INDEX_HTML);
}

static String arg(AsyncWebServerRequest* req, const char* name) {
    if (!req->hasParam(name, true)) return "";
    return req->getParam(name, true)->value();
}

void SetupServer::handleSave(AsyncWebServerRequest* req) {
    cfg_.tplinkSsid    = arg(req, "tpSsid");
    cfg_.tplinkPass    = arg(req, "tpPass");
    cfg_.fritzboxSsid  = arg(req, "fbSsid");
    cfg_.fritzboxPass  = arg(req, "fbPass");
    cfg_.pcMac         = arg(req, "pcMac");
    cfg_.tvIp          = arg(req, "tvIp");
    cfg_.tvMac         = arg(req, "tvMac");
    cfg_.dualsenseMac  = arg(req, "dsMac");
    // tvToken is left as-is; populated on first sequence run.
    cfg_.save();
    req->send(200, "text/plain", "saved — rebooting in 2s");
    delay(2000);
    ESP.restart();
}

void SetupServer::handlePairStart(AsyncWebServerRequest* req) {
    best_ds_mac_ = "";
    best_ds_rssi_ = -127;
    pairing_ = true;
    pair_started_at_ = millis();
    scanner_.start(30000);
    req->send(200, "text/plain", "scanning");
}

void SetupServer::handlePairStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["mac"] = best_ds_mac_;
    doc["rssi"] = best_ds_rssi_;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void SetupServer::handleReset(AsyncWebServerRequest* req) {
    Config::clear();
    req->send(200, "text/plain", "cleared — rebooting");
    delay(1000);
    ESP.restart();
}
```

- [ ] **Step 3: Smoke-test setup mode**

Replace `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"
#include "BleScanner.h"
#include "SetupServer.h"

constexpr int LED_PIN = 8;
StatusLed led;
Config cfg;
BleScanner scanner;
SetupServer* setup_srv = nullptr;

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);
  led.setState(LedState::Setup);

  Config::clear(); // force setup mode for this test
  cfg = Config::load();
  setup_srv = new SetupServer(cfg, scanner);
  setup_srv->begin();
}

void loop() { led.tick(millis()); delay(10); }
```

- [ ] **Step 4: Verify on device**

Run: `pio run -e esp32c3 -t upload && pio device monitor`
On your phone or laptop, join WiFi `esp32-tv-setup` (no password). Open `http://192.168.4.1/` (default ESP32 SoftAP IP). You should see the form. Click "Pair gamepad", power your DualSense on, watch the MAC populate. Fill the rest of the fields, submit. Serial should print `[setup] AP up...`, then form submission, then a reboot.

- [ ] **Step 5: Verify config persists**

After reboot, in `monitor`, the demo `main.cpp` clears NVS at boot (so will go back to setup). For now this is fine; Task 10 will use `Config::hasAll()` to actually dispatch.

- [ ] **Step 6: Restore main.cpp placeholder**

- [ ] **Step 7: Commit**

```bash
git add src/SetupServer.h src/SetupServer.cpp src/main.cpp
git commit -m "feat: SoftAP setup web server with gamepad pairing"
```

---

## Task 10: Wire main.cpp — boot dispatcher + cooldown

**Goal:** Replace `main.cpp` with the real boot logic: load config, branch into setup or runtime mode, in runtime mode run BLE scan with a sequence trigger and a 60-second cooldown.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Write the final `src/main.cpp`**

```cpp
#include <Arduino.h>
#include "StatusLed.h"
#include "Config.h"
#include "NetworkManager.h"
#include "BleScanner.h"
#include "TvController.h"
#include "Sequence.h"
#include "SetupServer.h"

constexpr int LED_PIN = 8;
constexpr uint32_t COOLDOWN_MS = 60000;

StatusLed led;
Config cfg;
NetworkManager net;
BleScanner scanner;

// Setup-mode owner; allocated only if config missing.
SetupServer* setup_srv = nullptr;

// Runtime-mode state
bool in_runtime_ = false;
uint32_t cooldown_until_ = 0;
volatile bool sequence_pending_ = false;

void enterSetupMode() {
  Serial.println("[boot] entering setup mode");
  led.setState(LedState::Setup);
  setup_srv = new SetupServer(cfg, scanner);
  setup_srv->begin();
}

void enterRuntimeMode() {
  Serial.println("[boot] entering runtime mode");
  net.configure(cfg.tplinkSsid, cfg.tplinkPass, cfg.fritzboxSsid, cfg.fritzboxPass);
  if (!net.connectTplink()) {
    Serial.println("[boot] could not reach TP-Link; staying error");
    led.setState(LedState::Error);
    return;
  }
  led.setState(LedState::Idle);
  in_runtime_ = true;

  scanner.begin();
  scanner.onHit([](const BleHit& h){
    if (millis() < cooldown_until_) return;
    String mac = h.mac; mac.toLowerCase();
    String wanted = cfg.dualsenseMac; wanted.toLowerCase();
    if (mac != wanted) return;
    sequence_pending_ = true;
    cooldown_until_ = millis() + COOLDOWN_MS;
    Serial.printf("[runtime] DualSense detected rssi=%d, queuing sequence\n", h.rssi);
  });
  scanner.start(0);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  led.attachPin(LED_PIN);

  cfg = Config::load();
  if (!cfg.hasAll()) {
    enterSetupMode();
  } else {
    enterRuntimeMode();
  }
}

void loop() {
  led.tick(millis());

  if (in_runtime_ && sequence_pending_) {
    sequence_pending_ = false;
    scanner.stop();
    Sequence seq({ &cfg, &net, &led });
    seq.run();
    // Restart scanning after sequence ends (network is back on TP-Link).
    scanner.start(0);
  }

  delay(10);
}
```

- [ ] **Step 2: End-to-end verification**

Pre-flight checklist on your hardware:
- PC powered off, WoL enabled in BIOS for the wired NIC.
- Samsung TV powered off, "Network Standby" / "Wake on LAN" enabled in TV settings.
- DualSense powered off.
- ESP32 plugged into USB (with no `Config::clear()` in code).
- NVS contains valid config (run setup mode first if needed).

Run: `pio run -e esp32c3 -t upload && pio device monitor`
Expected boot:
```
[boot] entering runtime mode
[net] connected ip=192.168.0.x
```
LED enters idle heartbeat. Press the PS button on the DualSense.
Expected:
```
[runtime] DualSense detected rssi=...
[seq] WoL→PC sent
[net] connected ip=192.168.178.x
[seq] WoL→TV sent
[tv] connected
[net] connected ip=192.168.0.x
```
Visually: PC begins booting; within ~5–10 s the TV is on with HDMI 3 selected; LED goes solid for 2 s, then back to heartbeat. Powering DualSense off and on again **within 60 s** must NOT re-fire the sequence.

- [ ] **Step 3: Verify cooldown expiry**

Wait at least 60 s after the last sequence, then power-cycle the DualSense. The sequence must fire again.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: runtime dispatcher with BLE-triggered sequence and cooldown"
```

---

## Task 11: Setup-mode "test sequence" endpoint

**Goal:** Allow forcing a sequence run from the web UI (useful when fixing things in the field without DualSense in hand).

**Files:**
- Modify: `src/SetupServer.h`
- Modify: `src/SetupServer.cpp`
- Modify: `src/main.cpp`

The test endpoint runs only when the device is in runtime mode (config is complete) **and** the user has temporarily switched the ESP32 onto TP-Link WiFi to reach it. Implementation: instead of running the sequence from inside the SoftAP-mode setup server, expose a small additional HTTP endpoint while in runtime mode on TP-Link.

- [ ] **Step 1: Add a tiny runtime HTTP endpoint to `src/main.cpp`**

Add at the top:
```cpp
#include <ESPAsyncWebServer.h>
AsyncWebServer runtime_http(80);
```

Inside `enterRuntimeMode()` after `scanner.start(0);` add:
```cpp
runtime_http.on("/trigger", HTTP_POST, [](AsyncWebServerRequest* req){
  sequence_pending_ = true;
  cooldown_until_ = millis() + COOLDOWN_MS;
  req->send(200, "text/plain", "queued");
});
runtime_http.on("/reset", HTTP_POST, [](AsyncWebServerRequest* req){
  Config::clear();
  req->send(200, "text/plain", "cleared — rebooting");
  delay(500);
  ESP.restart();
});
runtime_http.begin();
Serial.printf("[boot] runtime http on http://%s/\n", net.localIp().c_str());
```

- [ ] **Step 2: Verify**

Re-flash. From a device on the **TP-Link** WiFi, open `http://<esp32-ip>/trigger` with `curl -X POST`. Sequence must fire. Open `/reset` to wipe config and re-enter setup mode.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: runtime /trigger and /reset endpoints"
```

---

## Task 12: Pre-flight checklist documentation

**Goal:** A single short README that captures the non-firmware steps a future-you needs (PC WoL, TV Network Standby, locating MACs, finding SoftAP).

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write `README.md`**

```markdown
# esp32-tv

ESP32-C3 firmware that turns on your gaming PC and Samsung TV (HDMI 3) when you power on a DualSense controller.

## Hardware
- ESP32-C3 Super Mini, USB-C cable, always-on USB power source.

## One-time prerequisites
1. **PC**: enable Wake-on-LAN in BIOS for the wired NIC, and in Windows under
   Device Manager → wired NIC → Power Management → "Allow this device to wake the
   computer" + "Only allow a magic packet to wake the computer".
2. **Samsung Tizen TV**: enable Wake-on-LAN. Path varies by model:
   - Some models: Settings → General → Network → Expert Settings → Wake on LAN.
   - Newer models: Settings → General → External Device Manager → "Power on with mobile" / "Wake on LAN".
3. **MACs to gather**:
   - PC's wired-NIC MAC: `ipconfig /all` on Windows.
   - TV's WiFi MAC: TV menu Network Status, or Fritzbox UI → Home Network → Network.
   - DualSense MAC: captured automatically during pairing.

## First-run setup
1. Build & flash: `pio run -e esp32c3 -t upload`.
2. After boot, ESP32 (config-empty) opens AP `esp32-tv-setup` (open).
3. Connect a phone/laptop to that AP; open `http://192.168.4.1/`.
4. Fill the form. Use the "Pair gamepad" button; power on the DualSense within 30 s.
5. Submit. ESP32 reboots and tries to connect to TP-Link.
6. **TV pairing**: trigger one sequence (power on DualSense, or POST `/trigger` from TP-Link). The TV will prompt; accept on the remote. The token is saved to NVS automatically.

## Wiping config
From a TP-Link client: `curl -X POST http://<esp32-ip>/reset`.

## Build / dev
- Build:   `pio run -e esp32c3`
- Upload:  `pio run -e esp32c3 -t upload`
- Monitor: `pio device monitor`
- Tests:   `pio test -e native`
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README with prerequisites and first-run setup"
```

---

## Verification matrix

| What | How | Where |
| --- | --- | --- |
| LED state machine | `pio test -e native -f test_status_led` | Task 2 |
| WoL packet builder | `pio test -e native -f test_wol_packet` | Task 3 |
| WiFi hop | Serial log shows two distinct subnets | Task 5 |
| BLE detection | `[ble] hit` line on DualSense power-on | Task 6 |
| TV pairing | TV prompt + token in serial | Task 7 |
| Full sequence | PC + TV come up after BLE event | Task 10 |
| Cooldown | Re-trigger within 60 s ignored | Task 10 |
| Setup mode | SoftAP + form reachable | Task 9 |

End-to-end acceptance: with the DualSense, PC, and TV all powered off, pressing the PS button on the controller results in the PC at the login screen and the TV displaying the PC's HDMI 3 output within ~30 s.
