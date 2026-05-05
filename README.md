# esp32-sequencer

ESP32-C3 firmware: a generic *sequence builder* for triggering hardware
routines. Compose typed blocks (Wake-on-LAN, WiFi network hops, Samsung TV key
sends, waits, conditional branches, …) into named sequences and bind them to
triggers (HTTP routes, BLE MAC detection). Edit everything at runtime in a
small web UI; no recompile to change behaviour.

## Hardware

- ESP32-C3 Super Mini, USB-C cable, always-on USB power source.

## First-run setup

1. Build & flash: `pio run -e esp32c3 -t upload`.
2. After boot the device (no creds yet) opens AP `esp32-sequencer-setup`.
3. Connect a phone/laptop to that AP; open `http://192.168.4.1/`.
4. Enter the **idle WiFi** SSID + password — this is the network the device
   lives on and serves the UI from. Submit; it reboots and joins.
5. From a client on that network, open `http://<esp-ip>/edit` to build
   sequences and `http://<esp-ip>/triggers` to bind them.

The shipped default is a self-contained "Example sequence" using only
`divider`, `wait`, and `repeat` — no external hardware required to run.

## Bringing in real hardware

For a Samsung TV / gaming PC setup typical of the original use case:

- **PC**: enable Wake-on-LAN in BIOS for the wired NIC, and in Windows under
  Device Manager → wired NIC → Power Management → *"Allow this device to wake
  the computer"* + *"Only allow a magic packet to wake the computer"*.
- **Samsung Tizen TV**: enable WoL — *Settings → General → Network → Expert
  Settings → Wake on LAN*, or *Settings → General → External Device Manager →
  Power on with mobile* on newer models.
- **MACs to gather** (entered as block params in `/edit`, not the setup form):
  - PC's wired-NIC MAC: `ipconfig /all` on Windows.
  - TV's IP + WiFi MAC: TV menu *Network Status*, or your router's UI.
    *Gotcha*: many Samsung TVs report different MACs on WiFi vs LAN (last byte
    differs); WoL must target the *currently active* interface — verify with
    `arp -an | grep <tv-ip>` from a same-network host.
  - DualSense MAC (for `ble-mac` triggers): pair to your phone once and read
    it from BT settings, or use a BLE-scanner app.

## Wiping config

`curl -X POST http://<esp-ip>/reset` clears the idle WiFi creds and the
runtime-managed `tvToken`. Sequences and trigger bindings persist (different
NVS namespace), so your block params survive.

## Build / dev

- Build:   `pio run -e esp32c3`
- Upload:  `pio run -e esp32c3 -t upload`
- Monitor: `pio device monitor`
- Tests:   `pio test -e native`

### Code-quality tooling

Pre-commit hooks (file hygiene, clang-format, ruff, gitleaks, markdownlint,
conventional-commit message check):

```sh
uv sync                        # one-time: install dev deps
uv run pre-commit install --install-hooks
uv run pre-commit install --hook-type commit-msg
uv run pre-commit run --all-files
```

CI (`.github/workflows/ci.yml`) runs the same hooks plus `pio test -e native`
and the `esp32c3` firmware build on every PR.

## Sequence builder

A *sequence* is an ordered list of typed *blocks*: `wol`, `wifi-hop`,
`samsung-key`, `wait`, `repeat`, `if`, `divider`, `wait-for-trigger`. Sequences
are stored in NVS as JSON. *Triggers* (`http-route`, `ble-mac`) bind a route
or BLE detection to a sequence id and fire it at runtime.

- `http://<esp>/`         — landing page (sequences + triggers)
- `http://<esp>/edit`     — visual editor
- `http://<esp>/triggers` — trigger bindings
- `http://<esp>/settings` — bootstrap-only: idle-WiFi credentials

The setup page holds only what's needed to bring the device online and serve
the UI: the idle WiFi SSID + password (plus a runtime-managed TV pairing
token). Other networks the device hops to live as `wifi-hop` block params
inside sequences; per-device data (PC MAC, TV IP/MAC, DualSense MAC) lives as
block/trigger params. A sequence is the complete description of what to do.

API (JSON): `/api/schema`, `/api/sequences` (GET/PUT), `/api/triggers`
(GET/PUT), `/api/run?id=<id>` (POST), `/api/status` (GET).

To add a new block type for a different TV/PC/console, drop a single `.cpp`
file under `src/adapters/blocks/`, register it in the static-init `_Reg`
struct, and rebuild. Schemas are introspected at runtime so the editor picks
up the new block automatically.

## Diagnostics

**Input switching**: `KEY_HDMI3` isn't recognised on all Tizen models. The
robust recipe is: open the source picker (`KEY_SOURCE`), mash `KEY_LEFT` to
reach the leftmost entry, then `KEY_RIGHT` *N* times to reach HDMI*n*, then
`KEY_ENTER`. Compose this from individual `samsung-key` blocks (one per key)
plus `wait` blocks for settle delays, optionally inside a `repeat` for the
LEFT/RIGHT mash. The `samsung-key` block keeps a long-lived TV WebSocket
session, so successive keys reuse the connection.

**Ad-hoc key sends**: build a one-block sequence in `/edit` with a single
`samsung-key` and "Test run" it — that replaces the legacy `/tv-key?k=…`
diagnostic that was removed.
