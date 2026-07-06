# Seeed EE02 E-Ink Display Firmware

Custom firmware for the Seeed Studio XIAO ePaper Display Board (EE02) that displays photos from a [frame_server](https://github.com/josomm22/frame_server) on a 13.3" Spectra 6 color e-ink display.

## So what does this project do

Replaces the Seeed factory-installed firmware on the EE02 board with custom firmware that:

1. Connects to your WiFi network
2. Wakes up from deep sleep and fetches the next packed framebuffer from the frame server (`GET /next.bin`)
3. Displays the image on the 13.3" Spectra 6 e-ink screen
4. Goes back to sleep to conserve battery (configurable interval)
5. Skips wakeups during configurable quiet hours (like overnight when no one is seeing the display)
6. Updates itself over the air: each wake it asks the frame server whether a newer firmware version is published (`GET /firmware/version`) and, if so, downloads `GET /firmware/latest.bin` and flashes it — no USB cable needed after the first flash (see `firmware/README.md` for the endpoint contract and release flow)

The image side lives entirely in the [frame_server](https://github.com/josomm22/frame_server) repository: it pulls photos from Google Photos via the Picker API (or direct upload), dithers them for the Spectra 6 palette, and serves ready-to-display framebuffers. This repository is firmware only.

---

## How the two pieces fit together

```
[frame_server]                     [EE02 Board]
Node server (port 8765)            This firmware
      │                                  │
      │ GET /next.bin                    │
      │◄────────────────────────────────│ (wake from deep sleep)
      │                                  │
      │ Returns one random packed        │
      │ framebuffer from the queue       │
      │ (960,000 bytes, nibble4bpp)     │
      │────────────────────────────────►│
      │                                  │
      │                                  │ Remap palette indices to
      │                                  │ panel color codes
      │                                  │ Display image (dual UC8179 SPI)
      │                                  │
      │                                  │ Deep sleep (15 min default)
      │                                  ▼
```

The frame server picks a **random** image from its queue on every request, so each wakeup shows a (potentially) new photo. There is no hash/change-detection handshake — the firmware downloads and refreshes on every wake during active hours.

### The /next.bin data format

- 1600×1200 image, pre-rotated by the server into the panel's native 1200×1600 scan order
- 4 bits per pixel (2 pixels per byte, high nibble first) = 960,000 bytes
- Pixel values are **palette indices** in frame_server's order: `0=black 1=white 2=blue 3=green 4=red 5=yellow`
- The firmware remaps those indices to the UC8179 hardware codes (`0x00 black, 0x01 white, 0x02 yellow, 0x03 red, 0x05 blue, 0x06 green`) before pushing them to the panel

The firmware requires the frame server's default `nibble4bpp` packing (it does not decode `pack3bpp`).

---

## What's required

- A Seeed EE02 / XIAO ePaper board with a 13.3" Spectra 6 panel
- A USB-C data cable
- A 2.4 GHz WiFi network
- A running [frame_server](https://github.com/josomm22/frame_server) somewhere on your network (see that repo's README for setup)
- Python 3.13+ and `uv` (used only to install the PlatformIO build toolchain)

---

## Step-by-Step Setup Guide

### Step 1: Install `uv`

If you do not already have `uv`, install it with:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### Step 2: Download This Project

```bash
git clone <repository-url>
cd seeed_eink_board
```

### Step 3: Install the Build Toolchain

```bash
uv sync
```

This installs PlatformIO. The first firmware build later will also download the ESP32 toolchain, which takes a few minutes.

### Step 4: Configure Your WiFi Credentials

Copy the example config file and edit it with your credentials:

```bash
cd firmware/src
cp config.h.example config.h
```

Edit `config.h` and change these lines to match your WiFi network:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

**Note:**
- The ESP32 only supports **2.4GHz WiFi** (not 5GHz)
- Make sure to keep the quotes around the values

### Step 5: Point the Firmware at Your Frame Server

Edit `firmware/src/config_manager.h` and change the default host to wherever your frame server runs (e.g. your Zimaboard's LAN IP):

```cpp
#define DEFAULT_SERVER_HOST "192.168.86.34"  // Change this to your frame server's IP
```

The other defaults already match the frame server:
- `DEFAULT_SERVER_PORT 8765` - frame_server's default port
- `DEFAULT_IMAGE_ENDPOINT "/next.bin"` - the ESP32 endpoint
- `DEFAULT_SLEEP_MINUTES 15` - minutes between wakeups during active hours
- `DEFAULT_ACTIVE_START_HOUR 8` - local hour when refreshes begin
- `DEFAULT_ACTIVE_END_HOUR 20` - local hour when quiet hours begin
- `DEFAULT_TIMEZONE_OFFSET_MINUTES 0` - minutes from UTC, for example `-300` for EST without DST

All of these can be changed later without reflashing, via the on-device configuration web page (see below).

### Step 6: Build the Firmware

```bash
cd firmware
uv run pio run
```

The first build takes several minutes as it downloads the ESP32 compiler and libraries. Subsequent builds are much faster.

You should see:
```
========================= [SUCCESS] Took XX.XX seconds =========================
```

### Step 7: Connect the EE02 Board

1. Connect the EE02 board to your computer using a USB-C cable
2. **Note:** If nothing seems to be happening, your cable might be charge-only.

Check if you can see the device:

Linux:
```bash
ls /dev/ttyACM*
```

macOS:
```bash
ls /dev/cu.usb*
```

You should see something like `/dev/ttyACM0` (Linux), `/dev/cu.usbmodem14101` (macOS), or `COM3` (Windows).

You'll probably have to press the reset button (#4) on the board to upload the firmware.

### Step 8: Flash the Firmware

**Linux (adjust port if different):**
```bash
uv run pio run -t upload --upload-port /dev/ttyACM0
```

**macOS:**
```bash
uv run pio run -t upload --upload-port /dev/cu.usbmodem14101
```

You should see progress bars and finally:
```
========================= [SUCCESS] Took XX.XX seconds =========================
```

### Step 9: Queue Some Photos on the Frame Server

Follow the [frame_server README](https://github.com/josomm22/frame_server) to run the server, then open `http://YOUR_SERVER_IP:8765/pick` (Google Photos picker) or `http://YOUR_SERVER_IP:8765/upload` (direct upload) and add a few photos to the queue.

You can verify the queue works from any machine:

```bash
curl -o /dev/null -w "%{http_code} %{size_download} bytes\n" http://YOUR_SERVER_IP:8765/next.bin
```

A ready queue returns `200 960000 bytes`; an empty queue returns `404`.

### Step 10: Test the Display

Press the **reset button** on the EE02 board.

The display should:
1. Connect to WiFi (a few seconds)
2. Sync the clock via NTP (used for the quiet-hours schedule)
3. Download a random image from the queue (960 KB)
4. Refresh the display (usually 20-30 seconds of flickering)
5. Go to sleep

**Congratulations! (if that actually worked)** Your e-ink display should be showing your photo.

---

## Monitoring what's happening (serial output)

The ESP32 sends debug information over USB. Mainly helpful for troubleshooting.

### Using `screen` (Linux/macOS) or whatever you'd prefer

```bash
screen /dev/ttyACM0 115200
```

Press reset on the board to see output.

### Using PlatformIO Monitor

```bash
cd firmware
uv run pio device monitor --port /dev/ttyACM0 --baud 115200
```

### Following logs across deep sleep

The USB serial device disappears when the board enters deep sleep, so a single `pio device monitor` session usually stops after the first sleep cycle.

This loop reattaches each time the board wakes up:

```bash
cd firmware
while true; do
  uv run pio device monitor --port /dev/ttyACM0 --baud 115200
  sleep 1
done
```

### What You'll See

Normal operation looks like this:
```
========================================
Seeed EE02 E-Ink Display Firmware
========================================
Boot count: 1

========================================
NORMAL OPERATION MODE
========================================

Battery: ADC=2413, voltage=4.21V
Battery: ~100%
Connecting to WiFi: YourNetwork
.
Connected! IP: 192....
Waiting for NTP time sync...
Clock synchronized via NTP: 1772290800
Clock status: utc=1772290800, local=08:00, active_window=yes
Fetching image from: http://192.168.86.34:8765/next.bin
Sending X-Device-MAC: d0cf1326f7e8
Content length: 960000 bytes
Downloaded 960000 bytes in 10395 ms
Spectra6: Starting display refresh...
Spectra6: Refresh complete in 28432 ms
WiFi disconnected
Entering deep sleep for 15 minutes...
Going to sleep now...
```

When the queue is empty:
```
Fetching image from: http://192.168.86.34:8765/next.bin
Server queue is empty - add photos via the frame server's /pick or /upload page
Image fetch/display failed!
WiFi disconnected
Entering deep sleep for 15 minutes...
```

When the device wakes during quiet hours:
```
Clock status: utc=1772337600, local=21:00, active_window=no
Currently in quiet hours - skipping image fetch
WiFi disconnected
Outside active window - sleeping until next active start in 39600 seconds
Entering deep sleep for 660 minutes 0 seconds...
```

---

## Changing Settings Without Reflashing

You can change the server address, sleep interval, and other settings without reflashing the firmware!

### Entering Configuration Mode

1. Hold Button 1 (GPIO2 - the button closest to the USB connector)
2. While holding Button 1, press and release the reset button (Button 4 next to on/off switch)
3. Continue holding Button 1 for an additional second
4. Release Button 1 - the device will enter configuration mode

### Using the Web Configuration Interface

1. Connect to your WiFi network
2. The serial monitor will show the device's IP address
3. Open a web browser and go to that IP address (e.g., `http://192.168.86.24`)
4. You'll see a configuration page where you can change:
   - **Server Host:** The IP address (or hostname) of your frame server
   - **Server Port:** Usually 8765
   - **Image Endpoint:** Usually `/next.bin`
   - **Refresh Interval:** How often to fetch a new image during active hours (1-1440 minutes)
   - **Active Start / End Hour:** Local wall-clock active window
   - **Timezone Offset:** Minutes from UTC for local scheduling
5. Click **Save Configuration**
6. Click **Reboot Device**

### If WiFi Connection Fails

If the device can't connect to your WiFi in config mode:
1. It will create its own WiFi network called **"EInk-Setup"**
2. Connect your phone or computer to "EInk-Setup"
3. Open a browser to `http://192.168.4.1`
4. Configure the settings

---

## Troubleshooting

### "No such file or directory: /dev/ttyACM0"

The device isn't detected. Try:
1. **Different USB cable** - This is the most common issue! Many cables are charge-only.
2. **Press the reset button** - The device may be in deep sleep
3. **Check the port name** - Run `ls /dev/ttyACM*` (Linux) or `ls /dev/cu.usb*` (macOS)

### WiFi won't connect

- Make sure your network is **2.4GHz**
- Make sure you copied `config.h.example` to `config.h`
- Double-check the SSID and password in your `firmware/src/config.h`
- Rebuild and reflash after changing: `uv run pio run -t upload`

### "HTTP GET failed, code: -1"

The device can't reach the frame server:
1. Make sure the frame server is running (`docker compose up -d` or `npm run server` in the frame_server repo)
2. Check that the server IP address and port (8765) are correct
3. Make sure your firewall allows connections on port 8765
4. Test from another device: `curl -o /dev/null -w "%{http_code}\n" http://YOUR_SERVER_IP:8765/next.bin`

### "Server queue is empty"

The frame server has no photos queued. Open `http://YOUR_SERVER_IP:8765/pick` or `/upload` and add some. The previous image stays on the panel until the queue has content again.

### "Invalid content length: 720000"

The frame server was run with `--format=pack3bpp`. This firmware only decodes the server's default `nibble4bpp` format.

### Image is rotated or mirrored

Orientation is decided server-side: the frame server pre-rotates images into the panel's native scan order (`PANEL_ROTATION` / `PANEL_FLIP` in frame_server's `src/imaging/pipeline.ts`). If photos come out upside down or mirrored on your panel, adjust those constants in the frame server rather than the firmware.

### Wrong colors (e.g. blue and yellow swapped)

The firmware remaps the server's palette indices to panel color codes in `remapPaletteToPanel()` in `firmware/src/main.cpp`. If frame_server's palette order ever changes, update that table to match.

---

## File Structure

```
seeed_eink_board/
├── README.md              # This file
├── firmware/              # ESP32 firmware
│   ├── platformio.ini     # Build configuration
│   ├── README.md          # Detailed firmware documentation
│   └── src/
│       ├── config.h.example  # WiFi config template (copy to config.h)
│       ├── config_manager.h  # Default server settings
│       └── ...            # Other source files
└── pyproject.toml         # uv project that provides the PlatformIO toolchain
```

---

## Battery Monitoring

The firmware reads battery voltage on each wake cycle via the on-board voltage divider (GPIO1 ADC, enabled by GPIO6), estimates the charge percentage from a LiPo discharge curve, and sends both with every request as the `X-Battery-Voltage` and `X-Battery-Percent` HTTP headers, along with `X-Device-MAC` identifying the board and `X-Firmware-Version`. The frame server currently ignores these headers, but they're visible in any HTTP logs and available if the server grows per-device features (e.g. showing battery status on its home page).

### Voltage Levels

| Voltage | Capacity | Status |
|---------|----------|--------|
| 4.2V+   | Full (or on USB) | GOOD |
| 3.7V    | ~50% | GOOD |
| 3.3V    | ~10% | LOW |
| 3.0V    | Empty (cutoff) | LOW |

---

## Credits

- Image server: [frame_server](https://github.com/josomm22/frame_server)
- Firmware display driver inspired by [esphome-bigink](https://github.com/acegallagher/esphome-bigink)
