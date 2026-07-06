Seeed studios makes a development board called the XIAO ePaper Display Board - EE02 that is designed to drive an e ink spectra 6 13.3 inch display. The board is based on the ESP32-s3 chip and supports WiFi and Bluetooth connectivity.

The board is relatively new and there is limited documentation and community support available for it. However, Seeed Studio recently published this documentation: https://wiki.seeedstudio.com/getting_started_with_ee02/#getting-started-with-arduino

There is also a github repository that is trying to do the same thing we are in terms of directly addressing the EE02 XIAO ePaper Display Board at https://github.com/acegallagher/esphome-bigink.

Note that in this repository there is also documentation of the 13.3 inch spectra 6 driver: 13_3_E6_eInk_Display_module_Datasheet.pdf

This repository contains custom firmware for the ESP32 on the EE02 board. The image side lives in a separate repository: **frame_server** (https://github.com/josomm22/frame_server), a Node/TypeScript LAN server that pulls photos from Google Photos via the Picker API (or direct upload), processes them for the Spectra 6 panel (resize, tone map, dither, pack), and serves ready-to-display packed framebuffers. The old in-repo Python image server (`image_server.py`) has been removed.

## Custom Firmware Implementation

We have implemented custom Arduino/PlatformIO firmware in the `firmware/` directory:

### Architecture

```
[frame_server]                   [EE02 Board]
Node server :8765                Arduino Firmware
      │                                │
      │ GET /next.bin                  │
      │◄──────────────────────────────│ (wake from deep sleep)
      │                                │
      │ Returns one random packed      │
      │ framebuffer from the queue     │
      │ (960,000 bytes, nibble4bpp)   │
      │──────────────────────────────►│
      │                                │
      │                                │ Remap palette indices →
      │                                │ panel color codes
      │                                │ Display image
      │                                │ (dual-controller SPI)
      │                                │
      │                                │ Deep sleep (15 min default)
      │                                ▼
```

The ESP32 wakes up, connects to WiFi, syncs its clock via NTP (for the quiet-hours schedule), fetches `/next.bin` from the frame server, remaps the palette, displays the image, and goes back to sleep. The frame server picks a random image from its queue on each request, so there is no hash/change-detection handshake — every wake during active hours downloads and refreshes.

### Frame Server Protocol

- `GET /next.bin` — returns one random packed framebuffer (`application/octet-stream`, exactly 960,000 bytes) or `404` if the queue is empty (firmware keeps the previous image and goes back to sleep).
- `GET /firmware/version` — OTA check (see below); `404` = nothing published, check skipped silently.
- `GET /firmware/latest.bin` — OTA binary download.
- The firmware sends `X-Device-MAC`, `X-Firmware-Version`, `X-Battery-Voltage` (volts, 2 decimals), and `X-Battery-Percent` (integer 0-100, from a LiPo discharge curve; ≥4.2V/USB reads 100) request headers; the server currently ignores them.
- Photos are queued via the server's web UI: `/pick` (Google Photos picker) or `/upload` (direct upload).

**/next.bin format:**
- 4-bit per pixel, 2 pixels per byte, high nibble = first pixel; 960,000 bytes total
- Already in the panel's native scan order (the server pre-rotates via `PANEL_ROTATION = 270` in its `src/imaging/pipeline.ts`): 1600 rows × 600 bytes, where the first 300 bytes of each row belong to the master controller and the last 300 to the slave. The firmware streams the buffer without any transpose.
- Pixel values are **frame_server palette indices**, order fixed by its `src/imaging/palette.ts` (`aitjcizeSpectra6`): 0=black, 1=white, 2=blue, 3=green, 4=red, 5=yellow. The firmware remaps these to UC8179 hardware codes in `remapPaletteToPanel()` (`firmware/src/main.cpp`).
- The firmware only decodes the server's default `nibble4bpp` packing (a 720,000-byte response would be `pack3bpp`, which is rejected).
- Image orientation/mirroring is fixed server-side (`PANEL_ROTATION` / `PANEL_FLIP`), not in the firmware.

### Display Hardware Details

The 13.3" Spectra 6 display uses dual UC8179 controllers in master/slave configuration:

- **Master (CS=GPIO44)**: Top 600 pixel rows (0-599)
- **Slave (CS=GPIO41)**: Bottom 600 pixel rows (600-1199)
- Both controllers share CLK (GPIO7) and MOSI (GPIO9)

**Other GPIO pins:**
- DC: GPIO10
- Reset: GPIO38
- Busy: GPIO4 (HIGH when busy)
- Power: GPIO43

**Battery monitoring pins (same circuit as EE04 board):**
- Battery ADC: GPIO1 (A0) - voltage divider output
- ADC Enable: GPIO6 (A5) - set HIGH to enable voltage divider, LOW to save power

### Files

- `firmware/platformio.ini` - PlatformIO project configuration
- `firmware/src/config.h` - WiFi credentials and pin definitions (copy from `config.h.example`)
- `firmware/src/config_manager.h/.cpp` - Persistent configuration storage (NVS)
- `firmware/src/config_server.h/.cpp` - Web-based configuration interface
- `firmware/src/display.h/.cpp` - Spectra 6 display driver (ported from esphome-bigink)
- `firmware/src/version.h` - `FIRMWARE_VERSION` for OTA (bump on each release)
- `firmware/src/main.cpp` - Main loop: WiFi, NTP, OTA check, fetch, palette remap, display, deep sleep, config mode

### Runtime Configuration

The server endpoint is configurable at runtime without reflashing:

**To enter configuration mode:**
1. Hold Button 1 (GPIO2), press and release reset, keep holding Button 1 for one more second
2. The device will either:
   - Connect to your WiFi and show its IP address (open in browser)
   - Or create a WiFi network called "EInk-Setup" (connect and go to http://192.168.4.1)

**Configurable settings:**
- Server host (IP or domain name of the frame server)
- Server port (default 8765)
- Image endpoint path (default `/next.bin`)
- Refresh interval (how often to fetch a new image during active hours)
- Active window start/end hour and timezone offset (quiet hours)

Configuration is stored in NVS (Non-Volatile Storage) and persists across reboots. Defaults live in `firmware/src/config_manager.h`.

### OTA Firmware Updates

On every wake (after NTP, before the image fetch and the quiet-hours check), the firmware compares its compiled-in `FIRMWARE_VERSION` (`firmware/src/version.h`) against `GET /firmware/version` on the frame server. Any difference (plain string inequality, no semver) triggers a download of `GET /firmware/latest.bin` into the inactive OTA partition (`default_8MB.csv` = two ~3.3MB app slots) via the ESP32 `Update` library, MD5-verified against the optional `X-Firmware-MD5` response header, then reboot.

**Server contract (implemented in the frame_server repo, not here):** `/firmware/version` returns `200 text/plain` with a version string (1-32 chars, `[0-9A-Za-z._-]`) or `404` when nothing is published; `/firmware/latest.bin` returns the binary with correct `Content-Length` and optionally `X-Firmware-MD5` (32 hex chars).

**Release flow:** bump `FIRMWARE_VERSION` in `version.h` → `uv run pio run` → publish `.pio/build/seeed_xiao_esp32s3/firmware.bin` on the server under the same version string. The published version string MUST match the binary's compiled-in version; the firmware records the last flashed version in RTC memory (`lastFlashedVersion`, survives deep sleep and soft reset) and refuses to re-flash the same advertised version twice, so a mismatched upload logs a warning instead of update-looping.

### Quiet Hours / Clock

The firmware keeps a local wall-clock schedule (active window + timezone offset). Since the frame server has no time endpoint, the clock is synced via NTP (`pool.ntp.org`, `time.google.com`) after WiFi connects; the ESP32 RTC keeps approximate time through deep sleep, so subsequent wakes don't block on NTP. Outside the active window the device sleeps until the next window start.

### Building and Flashing

1. `uv sync` in the repo root installs the PlatformIO CLI (or use your own PlatformIO install)
2. Copy `firmware/src/config.h.example` to `firmware/src/config.h` and set WIFI_SSID and WIFI_PASSWORD
3. Optionally edit `firmware/src/config_manager.h` to change default server settings
4. Connect EE02 board via USB
5. Build and upload:
   ```bash
   cd firmware
   uv run pio run -t upload
   ```

### Battery Monitoring

The EE02 board has a voltage divider circuit (same as the EE04 board) that allows reading battery voltage via ADC:

- **GPIO1 (A0):** Battery voltage ADC input (through voltage divider)
- **GPIO6 (A5):** ADC enable pin - must be set HIGH before reading
- **Scaling factor:** 7.16 (voltage divider ratio, from EE04 reference)
- **Note:** GPIO1 is NOT a button despite earlier assumptions. The three physical keys on the board are on GPIO2, GPIO3, and GPIO5 (matching EE04 layout).

The firmware reads battery voltage once per boot (before WiFi to avoid ADC noise), converts it to a percentage via a piecewise-linear 1S LiPo discharge curve (`batteryPercentFromVoltage()` in `main.cpp`), and sends both via the `X-Battery-Voltage` and `X-Battery-Percent` HTTP headers. The frame server currently ignores them.

Typical LiPo voltage range: 3.0V (empty) to 4.2V (full). Readings above 4.2V indicate USB power.

### Color Palette

The Spectra 6 supports 6 colors. Two encodings matter:

| Color  | frame_server palette index | UC8179 hardware code |
|--------|----------------------------|----------------------|
| Black  | 0 | 0x00 |
| White  | 1 | 0x01 |
| Blue   | 2 | 0x05 |
| Green  | 3 | 0x06 |
| Red    | 4 | 0x03 |
| Yellow | 5 | 0x02 |

`remapPaletteToPanel()` in `firmware/src/main.cpp` converts the former to the latter after download. If frame_server's palette order ever changes, that table must change with it.

### Reference
- frame_server (image server this firmware talks to): https://github.com/josomm22/frame_server
- Seeed documentation: https://wiki.seeedstudio.com/getting_started_with_ee02/#getting-started-with-arduino
- Seeed GFX library (cloned locally at ~/Seeed_GFX): Contains the official T133A01 display driver. Our init register values and sequences have been verified to match exactly. The library defines this board/display combo as `BOARD_SCREEN_COMBO 510` with `USE_XIAO_EPAPER_DISPLAY_BOARD_EE02`.
- Display driver based on: https://github.com/acegallagher/esphome-bigink
- Battery ADC circuit based on EE04 documentation: https://wiki.seeedstudio.com/epaper_ee04/
