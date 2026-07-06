#ifndef VERSION_H
#define VERSION_H

// Firmware version, reported to the frame server as the X-Firmware-Version
// header and compared against GET /firmware/version for OTA updates.
//
// Release flow: bump this, build (`uv run pio run`), then publish
// .pio/build/seeed_xiao_esp32s3/firmware.bin with the SAME version string on
// the frame server's /firmware page. If the published version.txt doesn't
// match the version compiled into the binary, devices detect the mismatch
// after one flash and stop retrying (see lastFlashedVersion in main.cpp).
#define FIRMWARE_VERSION "1.0.0"

#endif // VERSION_H
