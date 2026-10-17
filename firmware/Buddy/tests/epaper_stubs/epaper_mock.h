#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <array>
#include <map>
#include <vector>
using esp_err_t = int;
using TickType_t = uint32_t;
using gpio_num_t = int;
using spi_host_device_t = int;
using spi_device_handle_t = void *;
constexpr int ESP_OK=0, GPIO_MODE_OUTPUT=1, GPIO_MODE_INPUT=2;
constexpr int GPIO_PULLUP_ENABLE=1, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
constexpr int SPI_DMA_CH_AUTO=0, MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
#define GPIO_IS_VALID_GPIO(x) ((x)<49)
#define GPIO_IS_VALID_OUTPUT_GPIO(x) ((x)<49)
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(...) ((void)0)
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
struct spi_bus_config_t { int miso_io_num,mosi_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz; };
struct spi_device_interface_config_t { int spics_io_num,clock_speed_hz,mode,queue_size; };
struct spi_transaction_t { size_t length; const void *tx_buffer; };
struct PanelMock {
    uint32_t tick=0;
    bool busy=false;
    int allocCalls=0,failAllocation=0,outstanding=0;
    int gpioCalls=0,failGpio=0,busError=0,addError=0;
    int buses=0,devices=0,frees=0,removes=0,transmits=0,failTransfer=0;
    std::array<int,64> levels={};
    std::vector<uint8_t> commands;
    std::map<uint8_t,std::vector<uint8_t>> data;
    uint8_t current=0;
};
inline PanelMock panelMock;
inline void *heap_caps_malloc(size_t n,int) {
    if (++panelMock.allocCalls==panelMock.failAllocation) return nullptr;
    ++panelMock.outstanding;return malloc(n);
}
inline void heap_caps_free(void *p) {
    if (p) { --panelMock.outstanding;free(p); }
}
inline const char *esp_err_to_name(int) { return "MOCK"; }
inline esp_err_t gpio_config(const gpio_config_t *) {
    return ++panelMock.gpioCalls==panelMock.failGpio ? -1 : ESP_OK;
}
inline esp_err_t gpio_set_level(gpio_num_t n,int v) {panelMock.levels.at(n)=v;return ESP_OK;}
inline int gpio_get_level(gpio_num_t) {return panelMock.busy?1:0;}
inline TickType_t xTaskGetTickCount() {return panelMock.tick;}
inline void vTaskDelay(TickType_t ticks) {panelMock.tick+=ticks;}
inline esp_err_t spi_bus_initialize(int,const spi_bus_config_t *cfg,int) {
    if(panelMock.busError)return panelMock.busError;
    if(cfg->max_transfer_sz!=5000)abort();
    ++panelMock.buses;return ESP_OK;
}
inline esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t *,spi_device_handle_t *out) {
    if(panelMock.addError)return panelMock.addError;
    ++panelMock.devices;*out=reinterpret_cast<void *>(1);return ESP_OK;
}
inline esp_err_t spi_bus_remove_device(spi_device_handle_t) {
    --panelMock.devices;++panelMock.removes;return ESP_OK;
}
inline esp_err_t spi_bus_free(int) {--panelMock.buses;++panelMock.frees;return ESP_OK;}
inline esp_err_t spi_device_polling_transmit(spi_device_handle_t,const spi_transaction_t *t) {
    if(++panelMock.transmits==panelMock.failTransfer)return -1;
    const auto *bytes=static_cast<const uint8_t *>(t->tx_buffer);
    if(panelMock.levels[10]==0) {panelMock.current=bytes[0];panelMock.commands.push_back(bytes[0]);}
    else panelMock.data[panelMock.current]=std::vector<uint8_t>(bytes,bytes+t->length/8);
    return ESP_OK;
}
