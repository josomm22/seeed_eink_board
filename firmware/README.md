# EE02 E-Ink Display Firmware

Custom firmware for the Seeed Studio XIAO ePaper Display Board (EE02) driving a 13.3" Spectra 6 e-ink display. It fetches packed framebuffers from a [frame_server](https://github.com/josomm22/frame_server) instance on your network.

## Features

- Fetches a random queued photo from the frame server (`GET /next.bin`) on every wake
- OTA firmware updates: checks the frame server for a newer published version on every wake and flashes itself
- Deep sleep between refreshes for battery conservation
- Quiet hours: skips refreshes outside a configurable local-time window (clock synced via NTP)
- Runtime configuration via web interface (no reflashing needed)
- Support for the 6-color Spectra 6 palette (Black, White, Yellow, Red, Blue, Green)

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension; `uv sync` in the repo root installs the CLI)
- USB-C cable with data lines (not charge-only)
- A running [frame_server](https://github.com/josomm22/frame_server) with photos in its queue

## Quick Start

### 1. Configure WiFi Credentials

Copy the example config and edit with your credentials:

```bash
cp src/config.h.example src/config.h
```

Edit `src/config.h` and set your WiFi credentials:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

### 2. Set Default Server Address (Optional)

Edit `src/config_manager.h` to set the default frame server:

```cpp
#define DEFAULT_SERVER_HOST "192.168.86.34"  // Your frame server's IP
#define DEFAULT_SERVER_PORT 8765
#define DEFAULT_IMAGE_ENDPOINT "/next.bin"
#define DEFAULT_SLEEP_MINUTES 15
#define DEFAULT_ACTIVE_START_HOUR 8
#define DEFAULT_ACTIVE_END_HOUR 20
#define DEFAULT_TIMEZONE_OFFSET_MINUTES 0
```

All of these can also be changed at runtime through the configuration web page.

### 3. Build the Firmware

```bash
cd firmware
uv run pio run
```

### 4. Flash the Firmware

Connect the EE02 board via USB. If the device is in deep sleep, press the reset button to wake it.

```bash
uv run pio run -t upload --upload-port /dev/ttyACM0
```

**Note:** The USB port may vary. On Linux it's typically `/dev/ttyACM0`, on macOS `/dev/cu.usbmodem*`, on Windows `COM3` or similar.

If the device isn't detected, try a different USB cable - many cables are charge-only and lack data lines.

### 5. Queue Photos on the Frame Server

See the [frame_server README](https://github.com/josomm22/frame_server) for server setup. Add photos via `http://your-server:8765/pick` (Google Photos) or `http://your-server:8765/upload` (direct upload).

### 6. Test

Press the reset button on the EE02 board. The display should:
1. Connect to WiFi
2. Sync the clock via NTP
3. Skip work and go back to sleep if it is currently in quiet hours
4. Download a random image from `/next.bin` (960 KB)
5. Refresh the display (takes 20-30 seconds)
6. Enter deep sleep

## How the Image Data Flows

The frame server does all image processing (resize, tone mapping, dithering) and serves the result as a packed framebuffer:

- **Size:** exactly 960,000 bytes (1600×1200 pixels, 4 bits per pixel)
- **Layout:** panel-native scan order — the server pre-rotates the image, so the firmware can stream the buffer to the controllers without transposing
- **Pixel values:** frame_server palette indices (`0=black 1=white 2=blue 3=green 4=red 5=yellow`)

The firmware's only transformation is `remapPaletteToPanel()` in `main.cpp`, which converts those indices to the UC8179 hardware color codes (see table below) before loading the display buffer. Each 600-byte buffer row is split down the middle: the first 300 bytes go to the master controller, the last 300 to the slave.

Every request includes `X-Device-MAC`, `X-Firmware-Version`, and `X-Battery-Voltage` headers. The frame server currently ignores them; they exist for logging and future per-device features.

## OTA Firmware Updates

On every wake (after WiFi + NTP, before the image fetch — so it also runs on quiet-hours wakes), the firmware checks the frame server for a published firmware version and updates itself if it differs from the running version.

### Server contract (to be implemented in the frame_server repo)

The firmware expects these two endpoints on the same host/port as `/next.bin`:

| Method | Path | Behavior |
|--------|------|----------|
| GET | `/firmware/version` | `200 text/plain` with the published version string (1-32 chars, `[0-9A-Za-z._-]`, trailing whitespace OK). `404` when no firmware is published — the firmware then skips the check silently. |
| GET | `/firmware/latest.bin` | `200 application/octet-stream` with the firmware binary and a correct `Content-Length`. Optionally an `X-Firmware-MD5` response header (32 hex chars) — if present, the firmware verifies the flash against it. |

Update decision: the firmware compares the served version string against its compiled-in `FIRMWARE_VERSION` (`src/version.h`). **Any difference** triggers an update (there is no ordering/semver logic), so publishing an older version rolls devices back — which is a feature.

Requests to both endpoints carry `X-Firmware-Version` (the running version), `X-Device-MAC`, and `X-Battery-Voltage` headers, useful for logging which devices are on which version.

### Update sequence

1. `GET /firmware/version` → differs from running version?
2. `GET /firmware/latest.bin` → streamed into the inactive OTA app partition via the ESP32 `Update` library (partition table `default_8MB.csv` provides two ~3.3 MB app slots)
3. MD5 verified (if the server sent `X-Firmware-MD5`), partition activated, reboot
4. The fresh boot reconnects and fetches the next image with the new firmware — the panel only refreshes once per wake even when an update happens

Any failure (timeout, short read, MD5 mismatch, no space) is logged, the update is aborted, and the device continues its normal image cycle; it retries on the next wake.

### Publishing a release

1. Bump `FIRMWARE_VERSION` in `src/version.h`
2. Build: `uv run pio run`
3. Publish `.pio/build/seeed_xiao_esp32s3/firmware.bin` on the frame server under the **same version string** you compiled in

**The published version string must match the binary's compiled-in `FIRMWARE_VERSION`.** If they differ, every wake would see "new version available" and re-flash forever. The firmware defends against this: it remembers the last version it flashed (in RTC memory, surviving deep sleep and the OTA reboot) and refuses to re-flash the same advertised version twice, logging the mismatch instead. A power cycle clears that memory.

## Monitoring Serial Output

The firmware outputs debug information via USB serial at 115200 baud.

### Using `cat` (simplest)

```bash
# Set baud rate and read output
stty -F /dev/ttyACM0 115200 raw -echo
cat /dev/ttyACM0
```

### Using `screen`

```bash
screen /dev/ttyACM0 115200
# Press Ctrl+A then K to exit
```

### Using PlatformIO Monitor

```bash
uv run pio device monitor --port /dev/ttyACM0 --baud 115200
```

### Important: Deep Sleep Disconnects USB

When the ESP32-S3 enters deep sleep, the USB connection is lost. This is normal behavior. To see output:

1. Start your serial monitor
2. Press the reset button on the board
3. Output will appear as the device boots

If you want the monitor to reconnect automatically after each sleep cycle:

```bash
while true; do
  uv run pio device monitor --port /dev/ttyACM0 --baud 115200
  sleep 1
done
```

### Example Output

```
========================================
Seeed EE02 E-Ink Display Firmware
========================================
Boot count: 1
Wakeup was not from deep sleep (code: 0)
ConfigManager: Initialized
Current Configuration:
  Server: 192.168.86.34:8765
  Endpoint: /next.bin
  Full URL: http://192.168.86.34:8765/next.bin
  Refresh interval: 15 minutes
  Active window: 08:00-20:00
  Timezone offset: 0 minutes from UTC

========================================
NORMAL OPERATION MODE
========================================

Battery: ADC=2413, voltage=4.21V
Connecting to WiFi: YourNetwork
.
Connected! IP: 192.168.86.24
Waiting for NTP time sync...
Clock synchronized via NTP: 1772290800
Clock status: utc=1772290800, local=08:00, active_window=yes
Spectra6: Initializing display...
Spectra6: Buffer allocated in PSRAM (960000 bytes)
Fetching image from: http://192.168.86.34:8765/next.bin
Sending X-Device-MAC: d0cf1326f7e8
Content length: 960000 bytes
Downloaded 960000 bytes in 10395 ms
Spectra6: Starting display refresh...
Spectra6: Data transfer complete in 1980 ms
Spectra6: Sending refresh command (this takes 20-30 seconds)...
Spectra6: Refresh complete in 28432 ms
WiFi disconnected
Entering deep sleep for 15 minutes 0 seconds...
Going to sleep now...
```

When the frame server's queue is empty:
```
Fetching image from: http://192.168.86.34:8765/next.bin
Server queue is empty - add photos via the frame server's /pick or /upload page
Image fetch/display failed!
WiFi disconnected
Entering deep sleep for 15 minutes 0 seconds...
```

When the device wakes during quiet hours:
```
Clock already valid - NTP refresh running in background
Clock status: utc=1772337600, local=21:00, active_window=no
Currently in quiet hours - skipping image fetch
WiFi disconnected
Outside active window - sleeping until next active start in 39600 seconds
Entering deep sleep for 660 minutes 0 seconds...
```

## Changing Configuration at Runtime

The firmware supports runtime configuration without reflashing.

### Entering Configuration Mode

**Hold Button 1 during reset:**
1. Hold Button 1 (GPIO2)
2. While holding, press and release the reset button
3. Continue holding Button 1 for an additional second
4. Release Button 1

The device will enter configuration mode and either:
- **STA mode**: Connect to your WiFi and show its IP address
- **AP mode**: Create a WiFi network called "EInk-Setup" if WiFi fails

### Web Configuration Interface

1. Open a browser to the device's IP address (shown in serial output)
   - Or connect to "EInk-Setup" WiFi and go to `http://192.168.4.1`

2. Configure these settings:
   - **Server Host**: IP address or domain name (e.g., `192.168.86.34`)
   - **Server Port**: Usually `8765`
   - **Image Endpoint**: Path to the image (e.g., `/next.bin`)
   - **Refresh Interval**: Minutes between wakeups during active hours (1-1440)
   - **Active Start Hour**: Local hour when refreshes begin (0-23)
   - **Active End Hour**: Local hour when quiet hours begin (0-23)
   - **Timezone Offset**: Minutes from UTC used for local wall-clock scheduling

3. Click "Save Configuration"

4. Click "Reboot Device" to start normal operation

### Configuration Persistence

Settings are stored in NVS (Non-Volatile Storage) and persist across:
- Reboots
- Deep sleep cycles
- Power loss

To reset to defaults, use the "Reset Defaults" button in the web interface.

## Troubleshooting

### Device not detected via USB

1. **Try a different USB cable** - Many cables are charge-only
2. Check if device appears: `ls /dev/ttyACM*` (Linux) or `ls /dev/cu.usb*` (macOS)
3. The device may be in deep sleep - press reset to wake it

### WiFi connection fails

- Verify SSID and password in your `src/config.h`
- Make sure you copied `config.h.example` to `config.h`
- Check that your network is 2.4GHz (ESP32 doesn't support 5GHz)
- Rebuild and reflash after changing credentials

### HTTP requests fail (code: -1)

- Verify the server is running: `curl -o /dev/null -w "%{http_code}\n" http://your-server:8765/next.bin`
- Check the server IP address matches your configuration
- Ensure firewall allows connections on port 8765

### Display doesn't refresh

- Check serial output for errors
- A `404` from `/next.bin` means the queue is empty - add photos via the frame server's web UI
- A content length of `720000` means the server packed in `pack3bpp` format; this firmware only decodes the default `nibble4bpp`
- The refresh takes 20-30 seconds - this is normal for this panel

### Image appears rotated or mirrored

Orientation is fixed server-side (`PANEL_ROTATION` / `PANEL_FLIP` in frame_server's `src/imaging/pipeline.ts`). Adjust there, not in the firmware.

### Wrong colors

If colors come out swapped (e.g. blue where yellow should be), the palette-index-to-panel-code table in `remapPaletteToPanel()` (`src/main.cpp`) no longer matches frame_server's palette order. Compare with `src/imaging/palette.ts` in the frame_server repo.

## File Structure

```
firmware/
├── platformio.ini          # PlatformIO project configuration
├── README.md               # This file
└── src/
    ├── config.h.example    # WiFi config template (copy to config.h)
    ├── config_manager.h    # Default server settings
    ├── config_manager.cpp  # NVS-based configuration storage
    ├── config_server.h     # Web configuration interface
    ├── config_server.cpp   # HTTP server for configuration
    ├── display.h           # Display driver interface
    ├── display.cpp         # Spectra 6 display driver
    ├── version.h           # FIRMWARE_VERSION (bump for each OTA release)
    └── main.cpp            # Main application logic
```

## Hardware Reference

### Pin Configuration (EE02 Board)

| Function | GPIO | Notes |
|----------|------|-------|
| SPI CLK | 7 | Shared by both controllers |
| SPI MOSI | 9 | Shared by both controllers |
| CS Master | 44 | Top half of display (rows 0-599) |
| CS Slave | 41 | Bottom half of display (rows 600-1199) |
| DC | 10 | Data/Command select |
| Reset | 38 | Hardware reset |
| Busy | 4 | LOW when busy, HIGH when ready |
| Power | 43 | Display power control |

### Display Specifications

- Resolution: 1600 x 1200 pixels
- Colors: 6 (Black, White, Yellow, Red, Blue, Green)
- Data format: 4-bit per pixel (2 pixels per byte)
- Buffer size: 960,000 bytes
- Refresh time: 20-30 seconds

### Color Codes

| Color | frame_server index | Hardware Code |
|-------|--------------------|---------------|
| Black | 0 | 0x00 |
| White | 1 | 0x01 |
| Blue | 2 | 0x05 |
| Green | 3 | 0x06 |
| Red | 4 | 0x03 |
| Yellow | 5 | 0x02 |

## Power Consumption

- **Active (WiFi + display refresh)**: ~150-200mA
- **Deep sleep**: ~10µA

For battery operation, increase the sleep interval to maximize battery life. Note that unlike the old hash-based setup, the firmware downloads and refreshes on every wake during active hours (the frame server serves a random image per request), so longer intervals matter more for battery life.

## Credits

- Image server: [frame_server](https://github.com/josomm22/frame_server)
- Display driver based on [esphome-bigink](https://github.com/acegallagher/esphome-bigink)
