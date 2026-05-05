# esp32-sequencer

ESP32-C3 firmware: a generic *sequence builder* for waking PCs/TVs, switching
HDMI inputs, etc. The default wake-everything routine turns on a gaming PC and
Samsung Tizen TV when a DualSense controller is detected over BLE.

## Hardware
- ESP32-C3 Super Mini, USB-C cable, always-on USB power source.

## One-time prerequisites
1. **PC**: enable Wake-on-LAN in BIOS for the wired NIC, and in Windows under
   Device Manager → wired NIC → Power Management → "Allow this device to wake the
   computer" + "Only allow a magic packet to wake the computer".
2. **Samsung Tizen TV**: enable Wake-on-LAN. Path varies by model:
   - Some models: Settings → General → Network → Expert Settings → Wake on LAN.
   - Newer models: Settings → General → External Device Manager → "Power on with mobile" / "Wake on LAN".
3. **MACs / IPs to gather** (entered into the editor, not the setup form):
   - PC's wired-NIC MAC: `ipconfig /all` on Windows.
   - TV's IP and WiFi MAC: TV menu Network Status, or Fritzbox UI → Home Network → Network. **Gotcha**: on many Samsung Tizen TVs, WiFi and LAN MACs differ in the last byte (e.g., `:20` vs `:21`). WoL must use the MAC for whichever interface the TV is currently on. Check the ARP table or run `arp -an | grep <tv-ip>` from the TP-Link to verify which MAC is active.
   - DualSense MAC: power on the controller and read it from your phone's BT settings, or use a BLE scanner app. Then enter it on the triggers page.

## First-run setup
1. Build & flash: `pio run -e esp32c3 -t upload`.
2. After boot, ESP32 (config-empty) opens AP `esp32-sequencer-setup` (open).
3. Connect a phone/laptop to that AP; open `http://192.168.4.1/`.
4. Fill in the **idle WiFi** SSID + password — this is the network where the
   ESP32 lives and serves the web UI. Submit; the device reboots and joins it.
5. From a client on that network, open `http://<esp32-ip>/edit` and complete the
   *wake-everything* sequence. Each `wifi-hop` block now takes its own SSID/
   password (and optional static IP/gateway); fill `wol` MACs and `samsung-keys`
   IP. The two `wifi-hop` blocks in the seed are for the PC's network (e.g. ICS
   over a TP-Link AP) and back to your idle network.
6. On `http://<esp32-ip>/triggers`, add a `ble-mac` trigger with your DualSense MAC.
7. **TV pairing**: trigger one run (power on DualSense or `POST /trigger`). The TV
   prompts once; accept on the remote. The token is saved to NVS automatically.

## Wiping config
`curl -X POST http://<esp32-ip>/reset` clears the idle WiFi creds + tvToken
(sequences and triggers stay, so block params you typed in are preserved).

## Build / dev
- Build:   `pio run -e esp32c3`
- Upload:  `pio run -e esp32c3 -t upload`
- Monitor: `pio device monitor`
- Tests:   `pio test -e native`

## Sequence builder

The wake-everything routine is one of many possible *sequences* — ordered lists
of typed *blocks* (`wol`, `wifi-hop`, `samsung-keys`, `wait`, `repeat`, `if`, …)
stored in NVS as JSON. Sequences are run by *triggers* (BLE MAC detection,
HTTP routes) which are also configured at runtime.

- `http://<esp>/`         — landing page (sequences + triggers)
- `http://<esp>/edit`     — visual editor
- `http://<esp>/triggers` — trigger bindings
- `http://<esp>/settings` — bootstrap-only: idle-WiFi credentials

The setup page holds only what's needed to bring the device online and serve
the UI: the idle WiFi SSID + password (plus a runtime-managed TV pairing
token). Every other network the device hops to lives as a `wifi-hop` block
parameter inside a sequence; per-device data (PC MAC, TV IP/MAC, DualSense MAC)
lives as block/trigger params. A sequence is the complete description of what
to do.

API (JSON): `/api/schema`, `/api/sequences` (GET/PUT), `/api/triggers` (GET/PUT),
`/api/run?id=<id>` (POST), `/api/status` (GET).

To add a new block type for a different TV/PC/console, drop a single
`.cpp` file under `src/adapters/blocks/`, register it in the static-init
`_Reg` struct, and rebuild. Schemas are introspected at runtime so the editor
picks up the new block automatically.

## Diagnostics
**Input switching**: `KEY_HDMI3` is not recognized on all Tizen models. The robust recipe is: open the source picker (`KEY_SOURCE`), mash `KEY_LEFT` to reach the leftmost entry, then `KEY_RIGHT` N times to reach HDMIn, then `KEY_ENTER`. This is what the default `wake-everything` sequence uses. To adjust counts for a different TV layout, edit the `samsung-keys` blocks in `/edit` (or `PUT /api/sequences` with the new tree).

**Ad-hoc key sends**: build a one-step sequence in `/edit` with a single `samsung-keys` block, save it, and "Test run" — that replaces the old `/tv-key?k=...` diagnostic.
