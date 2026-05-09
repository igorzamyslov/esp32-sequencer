# esp32-sequencer

ESP32-C3 firmware: a generic *sequence builder* for triggering hardware
routines. Compose typed blocks (Wake-on-LAN, WiFi network hops, Samsung TV key
sends, waits, conditional branches, …) into named sequences and bind them to
triggers (HTTP routes, BLE MAC detection). Edit everything at runtime in a
small web UI; no recompile to change behaviour.

## Hardware

- ESP32-C3 Super Mini

## Screenshots

### Main menu

<img width="772" height="712" alt="image" src="https://github.com/user-attachments/assets/0a66eed3-8b15-4cd3-b4ad-534641793cb5" />

### Sequence editor

<img width="1339" height="931" alt="image" src="https://github.com/user-attachments/assets/7678eeb2-a213-44fb-9e5e-150da22cbef4" />

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

## Personal use case

1. Connect to router wired to PC
2. Send WoL to PC
3. Connect to router connected to TV
4. Send WoL to TV
5. Navigate to required HDMI source

## Wiping config

`curl -X POST http://<esp-ip>/reset` clears the idle WiFi creds and the
runtime-managed `tvToken`. Sequences and trigger bindings persist (different
NVS namespace), so your block params survive.

## Build / dev

- Build:   `pio run -e esp32c3`
- Upload:  `pio run -e esp32c3 -t upload`
- Monitor: `pio device monitor`
- Tests:   `pio test -e native`

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
