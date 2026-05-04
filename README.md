# esp32-tv

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
2. After boot, ESP32 (config-empty) opens AP `esp32-tv-setup` (open).
3. Connect a phone/laptop to that AP; open `http://192.168.4.1/`.
4. Fill in WiFi credentials only (TP-Link + Fritzbox). Submit; the device reboots.
5. From a Fritzbox client, open `http://<esp32-ip>/edit` and fill the placeholder
   `wol` MACs and `samsung-keys` IP in the *wake-everything* sequence. Save.
6. On `http://<esp32-ip>/triggers`, add a `ble-mac` trigger with your DualSense MAC.
7. **TV pairing**: trigger one run (power on DualSense or `POST /trigger`). The TV
   prompts once; accept on the remote. The token is saved to NVS automatically.

## Wiping config
`curl -X POST http://<esp32-ip>/reset` clears WiFi creds + tvToken (sequences and
triggers stay, so the JSON-edited values you typed in are preserved).

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
- `http://<esp>/settings` — bootstrap-only WiFi credentials

The setup page only holds bootstrap values (WiFi creds + the runtime-managed TV
pairing token). All per-device parameters — PC MAC, TV IP/MAC, DualSense MAC —
are block/trigger params edited in `/edit` and `/triggers`. This makes blocks
self-contained: a sequence is the complete description of what to do.

API (JSON): `/api/schema`, `/api/sequences` (GET/PUT), `/api/triggers` (GET/PUT),
`/api/run?id=<id>` (POST), `/api/status` (GET).

To add a new block type for a different TV/PC/console, drop a single
`.cpp` file under `src/adapters/blocks/`, register it in the static-init
`_Reg` struct, and rebuild. Schemas are introspected at runtime so the editor
picks up the new block automatically.

## Diagnostics
**Input switching**: `KEY_HDMI3` is not recognized on all Tizen models. The robust recipe is: open the source picker (`KEY_SOURCE`), mash `KEY_LEFT` to reach the leftmost entry, then `KEY_RIGHT` N times to reach HDMIn, then `KEY_ENTER`. This is what the default `wake-everything` sequence uses. To adjust counts for a different TV layout, edit the `samsung-keys` blocks in `/edit` (or `PUT /api/sequences` with the new tree).

**Ad-hoc key sends**: build a one-step sequence in `/edit` with a single `samsung-keys` block, save it, and "Test run" — that replaces the old `/tv-key?k=...` diagnostic.
