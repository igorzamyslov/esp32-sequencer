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
   - TV's WiFi MAC: TV menu Network Status, or Fritzbox UI → Home Network → Network. **Gotcha**: On many Samsung Tizen TVs, WiFi and LAN MACs differ in the last byte (e.g., `:20` vs `:21`). WoL must use the MAC for whichever interface the TV is currently on. Check the ARP table on the router or run `arp -an | grep <tv-ip>` from the TP-Link to verify which MAC is active right now.
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

## Sequence-builder (Phase 1A)

The wake-everything routine is now a JSON sequence stored in NVS, runnable via
`POST /trigger` (default HTTP-route trigger) or by editing through the API:

- `GET /api/schema` — list available block, predicate, and trigger types.
- `GET /api/sequences` and `GET /api/triggers` — current configuration.
- `PUT /api/sequences` and `PUT /api/triggers` — replace the lists.
- `POST /api/run?id=<id>` — run one sequence on demand.

A web editor lands in Phase 1B.

## Diagnostics
**Input switching**: `KEY_HDMI3` is not recognized on all Tizen models. The robust recipe is: open the source picker (`KEY_SOURCE`), mash `KEY_LEFT` to reach the leftmost entry, then `KEY_RIGHT` N times to reach HDMIn, then `KEY_ENTER`. This is what the default `wake-everything` sequence uses. To adjust counts for a different TV layout, `PUT /api/sequences` with the changed `samsung-keys` blocks.

**TV key endpoint**: POST `/tv-key?k=KEY_NAME` or `/tv-key?k=KEY_A,KEY_B,KEY_C` (comma-separated keys) to test a key sequence on the TV. Connects, sends key(s), disconnects. Useful for debugging source switching on a specific model:
```
curl -X POST "http://<esp-ip>/tv-key?k=KEY_SOURCE,KEY_LEFT,KEY_LEFT,KEY_LEFT,KEY_LEFT,KEY_LEFT,KEY_LEFT,KEY_RIGHT,KEY_RIGHT,KEY_RIGHT,KEY_ENTER"
```
