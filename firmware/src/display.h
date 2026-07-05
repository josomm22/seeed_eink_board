#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <SPI.h>
#include "config.h"

/**
 * Seeed 13.3" Spectra 6 E-Paper Display Driver
 *
 * Hardware: Dual UC8179 controllers in master/slave configuration
 * Resolution: 1600 x 1200 pixels, 6 colors (Black, White, Red, Yellow, Blue, Green)
 *
 * Display Architecture:
 *     ┌─────────────────────────────────────┐
 *     │         MASTER (CS pin)             │  Rows 0-599 (top half)
 *     │         Top 600 pixel rows          │
 *     ├─────────────────────────────────────┤
 *     │         SLAVE (CS1 pin)             │  Rows 600-1199 (bottom half)
 *     │         Bottom 600 pixel rows       │
 *     └─────────────────────────────────────┘
 *           1600 pixels wide (full width)
 *
 * Data Format:
 * - 4-bit per pixel (2 pixels per byte), UC8179 color codes
 * - The buffer is stored in the panel's native scan order, matching the
 *   frame server's /next.bin layout: 1600 rows x 600 bytes, first 300 bytes
 *   of each row for the master controller, last 300 for the slave
 * - Total buffer size: 960,000 bytes
 */

class Spectra6Display {
public:
    Spectra6Display();

    // Initialize display hardware
    bool begin();

    // Load pre-packed 4bpp image data directly into buffer
    // Data should be 960,000 bytes in panel-native scan order with UC8179
    // color codes (i.e. /next.bin after remapPaletteToPanel)
    void loadImageData(const uint8_t* data, size_t length);

    // Display the current buffer contents
    void refresh();

    // Fill entire display with a single color
    void fillColor(uint8_t color);

    // Put display into deep sleep mode
    void sleep();

    // Get pointer to internal buffer (for direct manipulation)
    uint8_t* getBuffer() { return buffer_; }
    size_t getBufferSize() { return BUFFER_SIZE; }

private:
    // Frame buffer allocated in PSRAM
    uint8_t* buffer_;
    bool spiInitialized_;

    // Hardware control
    void hardwareReset();
    void initializeDisplay();
    void transferData();
    void refreshScreen();
    void powerOff();
    void displaySleep();

    // Wait for display to be ready
    bool waitUntilIdle(uint32_t timeoutMs);

    // SPI operations
    void spiBegin();
    void spiEnd();
    void spiWriteByte(uint8_t data);
    void spiWriteArray(const uint8_t* data, size_t len);

    // Dual-controller command helpers
    void bothCommand(uint8_t cmd);
    void bothCmdData(uint8_t cmd, const uint8_t* data, size_t len);
    void masterCommand(uint8_t cmd);
    void masterCmdData(uint8_t cmd, const uint8_t* data, size_t len);
    void slaveCommand(uint8_t cmd);
    void slaveCmdData(uint8_t cmd, const uint8_t* data, size_t len);
    void sendCmdDataWithCS(uint8_t cmd, const uint8_t* data, size_t len);
};

// Color codes for the Spectra 6 display
// These match the hardware codes used by the UC8179 controller
namespace Spectra6Color {
    const uint8_t BLACK  = 0x00;
    const uint8_t WHITE  = 0x01;
    const uint8_t YELLOW = 0x02;
    const uint8_t RED    = 0x03;
    const uint8_t BLUE   = 0x05;
    const uint8_t GREEN  = 0x06;
}

#endif // DISPLAY_H
