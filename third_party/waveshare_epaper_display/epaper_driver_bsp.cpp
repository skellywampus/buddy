// SPDX-FileCopyrightText: 2026 Buddy contributors
// SPDX-License-Identifier: MIT
//
// New ESP-IDF port of the licensed Waveshare V2 register sequences.
// See PROVENANCE.md and epaper_waveforms.h for upstream attribution.
// No board BSP or Unidex implementation is included here.
#include "epaper_driver_bsp.h"
#include "epaper_frame.h"
#include "epaper_waveforms.h"
#include "bounded_wait.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *Tag = "buddy-epaper";
void panelDelay(uint32_t ms) {
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}
}

epaper_driver_display::epaper_driver_display(int width, int height, custom_lcd_spi_t config)
    : config_(config) {
    // The gate scan count and waveform voltages below are specific to this panel.
    if (width != 200 || height != 200 ||
        !epaperFrameLayout(width, height, config.buffer_len)) {
        fault_ = true;
        ESP_LOGE(Tag, "Expected a 200 x 200 V2 panel and 5000-byte frame");
        return;
    }
    frame_ = static_cast<uint8_t *>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    previous_ = static_cast<uint8_t *>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!frame_ || !previous_) {
        fault_ = true;
        ESP_LOGE(Tag, "Unable to allocate display frames");
        heap_caps_free(frame_); frame_ = nullptr;
        heap_caps_free(previous_); previous_ = nullptr;
        return;
    }
    memset(frame_, 0xff, FrameBytes);
    memset(previous_, 0xff, FrameBytes);
    if (!configure()) {
        fault_ = true;
        releaseBus();
        heap_caps_free(frame_); frame_ = nullptr;
        heap_caps_free(previous_); previous_ = nullptr;
    }
}

epaper_driver_display::~epaper_driver_display() {
    releaseBus();
    heap_caps_free(frame_);
    heap_caps_free(previous_);
}

void epaper_driver_display::releaseBus() {
    if (device_) {
        // With polling transfers there are no outstanding queued transactions.
        const esp_err_t result = spi_bus_remove_device(device_);
        if (result != ESP_OK) {
            ESP_LOGE(Tag, "Display device release failed: %s", esp_err_to_name(result));
            return; // Never free a bus that still has a registered device.
        }
        device_ = nullptr;
    }
    if (own_bus_) {
        if (spi_bus_free(static_cast<spi_host_device_t>(config_.spi_host)) == ESP_OK)
            own_bus_ = false;
    }
}

bool epaper_driver_display::configure() {
    const uint8_t pins[] = {config_.cs, config_.dc, config_.rst,
                           config_.busy, config_.mosi, config_.scl};
    for (size_t i = 0; i < sizeof(pins); ++i) {
        if (!GPIO_IS_VALID_GPIO(pins[i]) || (i != 3 && !GPIO_IS_VALID_OUTPUT_GPIO(pins[i])))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (pins[i] == pins[j]) return false;
    }
    gpio_config_t lines = {};
    lines.pin_bit_mask = (1ULL << config_.cs) | (1ULL << config_.dc) | (1ULL << config_.rst);
    lines.mode = GPIO_MODE_OUTPUT;
    lines.pull_up_en = GPIO_PULLUP_ENABLE;
    lines.pull_down_en = GPIO_PULLDOWN_DISABLE;
    lines.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&lines) != ESP_OK || !pin(config_.cs, 1) ||
        !pin(config_.dc, 1) || !pin(config_.rst, 1)) return false;
    lines.pin_bit_mask = 1ULL << config_.busy;
    lines.mode = GPIO_MODE_INPUT;
    if (gpio_config(&lines) != ESP_OK) return false;

    spi_bus_config_t bus = {};
    bus.miso_io_num = -1;
    bus.mosi_io_num = config_.mosi;
    bus.sclk_io_num = config_.scl;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = FrameBytes;
    const auto host = static_cast<spi_host_device_t>(config_.spi_host);
    if (spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    own_bus_ = true;
    spi_device_interface_config_t device = {};
    device.spics_io_num = -1;
    device.clock_speed_hz = 10000000;
    device.mode = 0;
    device.queue_size = 1;
    return spi_bus_add_device(host, &device, &device_) == ESP_OK;
}

bool epaper_driver_display::pin(uint8_t number, int level) {
    if (fault_) return false;
    if (gpio_set_level(static_cast<gpio_num_t>(number), level) == ESP_OK) return true;
    fault_ = true;
    ESP_LOGE(Tag, "Display GPIO operation failed");
    return false;
}

bool epaper_driver_display::transfer(bool data, const uint8_t *bytes, size_t count) {
    if (!healthy()) return false;
    if (!bytes || count == 0 || count > FrameBytes) { fault_ = true; return false; }
    if (!pin(config_.dc, data ? 1 : 0) || !pin(config_.cs, 0)) return false;
    spi_transaction_t transaction = {};
    transaction.tx_buffer = bytes;
    transaction.length = count * 8;
    const esp_err_t result = spi_device_polling_transmit(device_, &transaction);
    // Always attempt to release CS, even if the transfer failed.
    const esp_err_t deselect = gpio_set_level(static_cast<gpio_num_t>(config_.cs), 1);
    if (result != ESP_OK || deselect != ESP_OK) {
        fault_ = true;
        ESP_LOGE(Tag, "Display SPI transfer failed");
        return false;
    }
    return true;
}

bool epaper_driver_display::reg(uint8_t command, const uint8_t *data, size_t count) {
    return transfer(false, &command, 1) && (!count || transfer(true, data, count));
}

bool epaper_driver_display::idle() {
    if (!healthy()) return false;
    const bool ready = epaperWaitIdle(
        [&] { return gpio_get_level(static_cast<gpio_num_t>(config_.busy)) == 1; },
        [] { return static_cast<uint32_t>(xTaskGetTickCount()); },
        [] { panelDelay(5); }, pdMS_TO_TICKS(5000));
    if (!ready) {
        fault_ = true;
        ESP_LOGE(Tag, "Display BUSY timeout");
    }
    return ready;
}

bool epaper_driver_display::resetPanel() {
    if (!healthy() || !pin(config_.rst, 1)) return false;
    panelDelay(50);
    if (!pin(config_.rst, 0)) return false;
    panelDelay(20);
    if (!pin(config_.rst, 1)) return false;
    panelDelay(50);
    return idle();
}

bool epaper_driver_display::waveform(const uint8_t *table) {
    return reg(0x32, table, 153) && idle() &&
           reg(0x3f, table + 153, 1) && reg(0x03, table + 154, 1) &&
           reg(0x04, table + 155, 3) && reg(0x2c, table + 158, 1);
}

bool epaper_driver_display::activate(uint8_t control) {
    return reg(0x22, &control, 1) && reg(0x20) && idle();
}

void epaper_driver_display::EPD_Init() {
    static const uint8_t gate[] = {0xc7, 0x00, 0x01};
    static const uint8_t entry[] = {0x01}, x[] = {0x00, 0x18};
    static const uint8_t y[] = {0xc7, 0x00, 0x00, 0x00};
    static const uint8_t border[] = {0x01}, sensor[] = {0x80};
    static const uint8_t cursorX[] = {0x00}, cursorY[] = {0xc7, 0x00};
    if (resetPanel() && reg(0x12) && idle() &&
        reg(0x01, gate, sizeof(gate)) && reg(0x11, entry, sizeof(entry)) &&
        reg(0x44, x, sizeof(x)) && reg(0x45, y, sizeof(y)) &&
        reg(0x3c, border, sizeof(border)) && reg(0x18, sensor, sizeof(sensor)) &&
        activate(0xb1) && reg(0x4e, cursorX, sizeof(cursorX)) &&
        reg(0x4f, cursorY, sizeof(cursorY)) && idle() && waveform(WF_Full_1IN54))
        mode_ = Mode::Full;
}

void epaper_driver_display::EPD_Clear() {
    if (frame_) memset(frame_, 0xff, FrameBytes);
}

bool epaper_driver_display::fullFrame(bool bothBanks) {
    if (mode_ != Mode::Full) EPD_Init();
    if (!healthy() || !reg(0x24, frame_, FrameBytes) ||
        (bothBanks && !reg(0x26, frame_, FrameBytes)) || !activate(0xc7)) return false;
    memcpy(previous_, frame_, FrameBytes);
    baseline_valid_ = true;
    return true;
}

void epaper_driver_display::EPD_Display() { fullFrame(false); }
void epaper_driver_display::EPD_DisplayPartBaseImage() { fullFrame(true); }

void epaper_driver_display::EPD_Init_Partial() {
    static const uint8_t options[] = {0, 0, 0, 0, 0, 0x40, 0, 0, 0, 0};
    static const uint8_t border[] = {0x80};
    if (resetPanel() && waveform(WF_PARTIAL_1IN54_0) &&
        reg(0x37, options, sizeof(options)) && reg(0x3c, border, sizeof(border)) &&
        activate(0xc0)) {
        mode_ = Mode::Partial;
        EPD_PowerOff(); // Controller RAM/LUT remain available for the next update.
    }
}

void epaper_driver_display::EPD_DisplayPart() {
    if (!healthy()) return;
    if (!baseline_valid_) {
        if (fullFrame(true)) EPD_Init_Partial();
        return;
    }
    if (mode_ != Mode::Partial) EPD_Init_Partial();
    if (reg(0x26, previous_, FrameBytes) && reg(0x24, frame_, FrameBytes) && activate(0xcf))
        memcpy(previous_, frame_, FrameBytes);
}

void epaper_driver_display::EPD_PowerOff() {
    activate(0x03); // SSD1681 RAM-retaining analog/clock standby, not deep sleep.
}

void epaper_driver_display::EPD_DrawColorPixel(uint16_t x, uint16_t y, uint8_t color) {
    if (!fault_)
        epaperWritePixel(frame_, FrameBytes, 25, 200, 200, x, y, color != DRIVER_COLOR_WHITE);
}
