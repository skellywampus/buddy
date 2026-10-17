// SPDX-FileCopyrightText: 2026 Buddy contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

enum COLOR_IMAGE {
    DRIVER_COLOR_WHITE = 0xff,
    DRIVER_COLOR_BLACK = 0x00,
    FONT_BACKGROUND = DRIVER_COLOR_WHITE
};

// Compatibility with Buddy's existing board configuration.
struct custom_lcd_spi_t {
    uint8_t cs, dc, rst, busy, mosi, scl;
    int spi_host;
    int buffer_len;
};

// ESP-IDF adapter for the 200 x 200 monochrome V2 controller only.
// Register initialization/waveforms are ported from the pinned MIT Waveshare
// reference described in PROVENANCE.md. Callers own display scheduling.
class epaper_driver_display {
public:
    epaper_driver_display(int width, int height, custom_lcd_spi_t config);
    ~epaper_driver_display();
    epaper_driver_display(const epaper_driver_display &) = delete;
    epaper_driver_display &operator=(const epaper_driver_display &) = delete;

    bool healthy() const { return !fault_ && device_ != nullptr; }
    void EPD_Init();
    void EPD_Clear();
    void EPD_Display();
    void EPD_DisplayPartBaseImage();
    void EPD_Init_Partial();
    void EPD_DisplayPart();
    void EPD_PowerOff();
    void EPD_DrawColorPixel(uint16_t x, uint16_t y, uint8_t color);

private:
    enum class Mode { Uninitialized, Full, Partial };
    static constexpr size_t FrameBytes = 5000;
    const custom_lcd_spi_t config_;
    spi_device_handle_t device_ = nullptr;
    uint8_t *frame_ = nullptr;
    uint8_t *previous_ = nullptr;
    bool own_bus_ = false;
    bool fault_ = false;
    bool baseline_valid_ = false;
    Mode mode_ = Mode::Uninitialized;

    bool configure();
    void releaseBus();
    bool pin(uint8_t number, int level);
    bool transfer(bool data, const uint8_t *bytes, size_t count);
    bool reg(uint8_t command, const uint8_t *data = nullptr, size_t count = 0);
    bool idle();
    bool resetPanel();
    bool waveform(const uint8_t *table);
    bool activate(uint8_t control);
    bool fullFrame(bool bothBanks);
};
