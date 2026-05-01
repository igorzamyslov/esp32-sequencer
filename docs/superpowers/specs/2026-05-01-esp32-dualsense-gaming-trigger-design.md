# ESP32 DualSense → Gaming-mode Trigger — Design

**Date:** 2026-05-01
**Status:** Draft
**Hardware:** ESP32-C3 Super Mini

## Goal

When the user powers on a Sony DualSense controller, a nearby ESP32 detects it
via BLE and automatically:

1. Wakes the gaming PC (Wake-on-LAN).
2. Powers on the Samsung Tizen TV.
3. Switches the Samsung TV to HDMI 3 (the PC).

No physical button on the ESP32. No phone app. The act of picking up the
controller and pressing the PS button *is* the trigger.

Out of scope for this iteration: powering anything off, controlling the Fire
TV, supporting multiple gamepads, supporting other TV inputs.

## Environment & Constraints

### Network topology

- **Fritzbox** (WiFi, e.g. `192.168.178.0/24`) — Samsung TV and Fire TV are
  on this network.
- **TP-Link** (separate subnet, e.g. `192.168.0.0/24`) — gaming PC's wired NIC
  is on this network. The PC bridges Fritzbox WiFi → TP-Link wired side via
  Internet Connection Sharing (ICS).
- The PC's **wired NIC** is the one that handles WoL. It is only reachable from
  TP-Link clients. When the PC is off, the bridge is down, so devices on the
  Fritzbox side cannot reach TP-Link.

### Devices

- **PC**: wired to TP-Link, also on Fritzbox WiFi (when on). Wakes via WoL
  magic packet to the wired NIC.
- **Samsung Tizen TV**: on Fritzbox WiFi. Controlled via the local Tizen
  WebSocket API (`wss://<tv-ip>:8002/api/v2/channels/samsung.remote.control`).
  First connection requires a one-time approval prompt on the TV; the resulting
  token is reused indefinitely.
- **Fire TV**: on HDMI 1, on Fritzbox WiFi. **Untouched** by this project.
- **DualSense (PS5 controller)**: emits BLE advertising packets when powered
  on. ESP32 watches for its MAC.
- **ESP32-C3 Super Mini**: BLE 5 + WiFi 4 (2.4 GHz). Always plugged into USB
  power, idle scanning.
- **Samsung HDMI mapping**: PC = HDMI 3, Fire TV = HDMI 1.

## Architecture

The ESP32 has one of two roles at any time:

| Mode             | WiFi connection | BLE         | Purpose                        |
| ---------------- | --------------- | ----------- | ------------------------------ |
| Idle (default)   | TP-Link         | Scanning    | Wait for DualSense; ready to WoL |
| Sequence-running | (switching)     | Off         | Run the trigger sequence       |
| Setup            | Own SoftAP      | On-demand   | First-run config via web UI    |

### Trigger sequence (on DualSense detected)

1. **WoL → PC**: still on TP-Link → broadcast magic packet to PC's wired-NIC
   MAC. Fire-and-forget, UDP, no ack expected.
2. **Switch network**: disconnect TP-Link → connect Fritzbox (~3–5 s).
3. **WoL → TV**: broadcast magic packet to the Samsung TV's WiFi MAC on the
   Fritzbox subnet. This is what actually powers a fully-off Tizen TV on; the
   WebSocket port isn't reachable until the TV has woken. Idempotent — if the
   TV is already on, the packet is harmless. **Requires "Wake on LAN /
   Network Standby" to be enabled in the TV's settings; flagged as a setup
   prerequisite.**
4. **TV WebSocket**: connect to `wss://<tv-ip>:8002/...`. Retry every 1 s for
   up to 15 s while the TV finishes booting. On final failure, error LED,
   abort sequence.
5. **TV input**: send `KEY_HDMI3` (Tizen 2018+). On older Tizen firmware where
   that key is unsupported, fall back to `KEY_SOURCE` then enough arrow presses
   + `KEY_ENTER` to land on HDMI 3.
6. **Reset**: close WebSocket → disconnect Fritzbox → reconnect TP-Link →
   resume BLE scanning.
7. **Cooldown**: ignore further DualSense detections for 60 s so we don't
   re-fire if the user toggles the controller.

Note: we never send `KEY_POWER`. WoL handles power-on; sending `KEY_POWER`
when the TV is already on would turn it *off*, which is the opposite of what
we want.

The PC and TV power-on happen in parallel-ish: WoL is sent within the first
second; TV control begins after the WiFi hop. PC boot (~30 s) and TV warm-up
(~5 s) overlap, so the user perceives "everything turning on at once."

### Components (Arduino + PlatformIO)

```
+--------------------+   +--------------------+   +-------------------+
|  BLE scanner       |   |  Network manager   |   |  TV controller    |
|  (NimBLE-Arduino)  |   |  (WiFi.h)          |   |  (arduinoWebSocket|
|                    |   |                    |   |   + WiFiClient    |
|  scan() -> match?  |   |  to_tplink()       |   |   Secure)         |
+---------+----------+   |  to_fritzbox()     |   |                   |
          |              |  send_wol(mac)     |   |  pair() (one-shot)|
          v              +---------+----------+   |  power_on()       |
+--------------------+             |              |  switch_to_hdmi3()|
|  Sequence          |             |              +---------+---------+
|  orchestrator      |<------------+------------------------+
|                    |
|  on_match(mac):    |             +-------------------+
|    wol -> hop ->   |<----------- |  Status LED       |
|    tv_on -> hdmi3  |             |  (non-blocking)   |
|    -> reset        |             +-------------------+
+----------+---------+
           |
           v
+--------------------+
|  Config (NVS)      |
|  - WiFi creds x2   |
|  - PC MAC          |
|  - TV IP, MAC,     |
|    pairing token   |
|  - DualSense MAC   |
+--------------------+
```

Each component is a small C++ class with a narrow public interface, no global
state, and no knowledge of components above it in the call graph.

### Setup / first-run flow

1. ESP32 boots with no config → opens a SoftAP `esp32-tv-setup` + simple HTTP
   server.
2. Captive page collects:
   - TP-Link SSID + password
   - Fritzbox SSID + password
   - PC's wired-NIC MAC address
   - Samsung TV IP address **and WiFi MAC address** (the latter for WoL)
3. "Pair gamepad" button → ESP32 enters BLE-scan-and-capture mode for 30 s.
   The user is instructed to power on **only** the DualSense during that
   window. ESP32 filters advertising packets by Sony manufacturer ID
   (`0x054C`) and/or the device name `Wireless Controller`, then picks the
   single strongest-RSSI candidate seen during the window and stores its MAC.
   If multiple candidates appear, the page shows them sorted by RSSI and the
   user picks one.
4. "Pair TV" button → ESP32 connects to TP-Link, hops to Fritzbox, opens a
   WebSocket to the TV. User accepts the prompt on the TV remote. Token saved
   to NVS.
5. ESP32 reboots into idle/scanning mode.

A `/reset` endpoint (only reachable while connected to TP-Link) re-enters
setup mode and clears NVS.

### Status feedback (onboard LED, GPIO 8, active LOW)

| State               | Pattern                              |
| ------------------- | ------------------------------------ |
| Setup mode (no cfg) | Slow on/off (1 Hz)                   |
| Idle, scanning      | Heartbeat (brief blip every ~3 s)    |
| Sequence running    | Fast blink (5 Hz)                    |
| Success             | Solid on for 2 s                     |
| Error               | Three short pulses, then back to idle |

### Error handling

- **WoL**: no ack possible; treat as "sent." We don't know if the PC actually
  woke. Acceptable — if WoL fails, user notices and presses again.
- **WiFi connect fail** (either network): 3 retries with exponential backoff
  (1 s, 2 s, 4 s). On final failure, error LED, return to idle.
- **WebSocket connect fail**: 2 retries with 2 s gap. On failure, error LED,
  abort sequence (don't try HDMI switch).
- **`KEY_HDMI3` rejected**: fall back to `KEY_SOURCE` + arrow + ENTER. If even
  that fails, leave TV on its last input, error LED.
- **DualSense MAC missing from NVS**: stay in setup mode; LED reflects setup
  mode pattern.
- **Cooldown**: a 60 s post-sequence window during which DualSense detections
  are ignored. Prevents re-firing if the user power-cycles the controller.

## Testing strategy

Each module is testable in isolation on the ESP32 with serial logging:

- **BLE scanner**: power DualSense on/off; verify match log lines and RSSI.
- **Network manager**: trigger network hop manually via a `/test/hop` HTTP
  endpoint; verify timing and recovery on failure.
- **WoL**: trigger via `/test/wol`; capture packet on a laptop running
  `tcpdump -i any port 9` or watch PC wake.
- **TV controller**: `/test/tv-power`, `/test/tv-hdmi3` endpoints. First call
  drives the pairing flow.
- **Sequence orchestrator**: `/test/full-sequence` endpoint to run end-to-end
  without the BLE trigger.
- **Cooldown**: scripted detection + immediate re-trigger; second one must be
  ignored.

End-to-end verification: DualSense off, PC off, TV off → press PS button →
within ~10 s, TV is on HDMI 3; within ~30 s, PC is at the login screen.

## Open risks

- **ESP32-C3 Super Mini WiFi range**: clones with weak PCB antenna may struggle
  to reach Fritzbox or TP-Link. Mitigation: place the ESP32 within line-of-sight
  / short distance of both APs, or swap to a board with better antenna.
- **WiFi hop time**: connecting to a new SSID can take 3–10 s on ESP32-C3. If
  the TV power-on attempt happens too soon after the hop, it'll fail. The
  retry loop in step 3 absorbs this.
- **Tizen API drift**: Samsung occasionally tweaks the WebSocket protocol
  between firmware versions. Token may invalidate on TV firmware updates,
  requiring a re-pair.
- **DualSense advertising visibility**: the controller advertises at boot and
  while looking for a host. If it's already paired and quickly connects to the
  PC, the BLE window the ESP32 sees may be brief — but we only need a couple
  of advertising packets, which arrive within ~100 ms of power-on. Mitigation:
  passive scanning (no scan-response requests), low scan interval, accept
  matches from a wider RSSI range.
- **Bridge not yet up when sequence starts**: not actually a problem — the
  ESP32's WoL goes out on the TP-Link side directly, not through the bridge,
  and TV control happens via Fritzbox directly. We never depend on the bridge.

## Defaults committed

- Framework: **Arduino + PlatformIO**.
- Board: **ESP32-C3 Super Mini** (esp32-c3-devkitm-1 PlatformIO board).
- Libraries: **NimBLE-Arduino** (BLE), **WiFi.h** (built-in),
  **WebSocketsClient** by Links2004 (Tizen WebSocket).
- Config storage: **Preferences** library (NVS wrapper).
- HTTP server (setup + test endpoints): **ESPAsyncWebServer**.
- LED: onboard, GPIO 8, active LOW.
- Cooldown: 60 s.
- BLE scan: passive, 100 ms interval, 50 ms window, continuous.
