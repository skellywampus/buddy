// Execute the production ESP-IDF adapter against deterministic peripheral faults.
#include <cassert>
#include <cstdio>
#include <type_traits>
#include "epaper_stubs/epaper_mock.h"
#include "../../../third_party/waveshare_epaper_display/epaper_driver_bsp.cpp"
static const custom_lcd_spi_t config={11,10,9,8,13,12,2,5000};
static_assert(!std::is_copy_constructible<epaper_driver_display>::value);
static void clean() {
    assert(panelMock.outstanding==0 && panelMock.buses==0 && panelMock.devices==0);
    panelMock={};
}
int main() {
    {
        epaper_driver_display invalid(16,16,config);
        assert(!invalid.healthy() && panelMock.allocCalls==0 && panelMock.gpioCalls==0);
    }
    clean();
    for(int allocation=1;allocation<=2;++allocation) {
        panelMock.failAllocation=allocation;
        {epaper_driver_display display(200,200,config);assert(!display.healthy());
         assert(panelMock.outstanding==0);display.EPD_Init();}
        clean();
    }
    for(int stage=1;stage<=4;++stage) {
        if(stage<3)panelMock.failGpio=stage;
        if(stage==3)panelMock.busError=-1;
        if(stage==4)panelMock.addError=-1;
        {epaper_driver_display display(200,200,config);assert(!display.healthy());
         assert(panelMock.outstanding==0);}
        clean();
    }
    {
        auto bad=config;bad.busy=bad.cs;
        epaper_driver_display display(200,200,bad);
        assert(!display.healthy() && panelMock.gpioCalls==0);
    }
    clean();
    for(int round=0;round<2;++round) {
        {
            epaper_driver_display display(200,200,config);
            assert(display.healthy() && panelMock.buses==1 && panelMock.devices==1);
            display.EPD_Init();
            assert(display.healthy());
            const std::vector<uint8_t> expected={0x12,0x01,0x11,0x44,0x45,0x3c,
                0x18,0x22,0x20,0x4e,0x4f,0x32,0x3f,0x03,0x04,0x2c};
            assert(panelMock.commands==expected);
            assert((panelMock.data[0x01]==std::vector<uint8_t>{0xc7,0,1}));
            assert((panelMock.data[0x45]==std::vector<uint8_t>{0xc7,0,0,0}));
            assert(panelMock.data[0x32].size()==153);
            display.EPD_Display();
            assert(panelMock.data[0x24].size()==5000);
            assert(panelMock.data[0x24][0]==0xff);
            display.EPD_Init_Partial();
            assert(panelMock.data[0x22][0]==0x03); // Standby after partial setup.
            display.EPD_DrawColorPixel(0,0,DRIVER_COLOR_BLACK);
            display.EPD_DrawColorPixel(199,199,DRIVER_COLOR_BLACK);
            display.EPD_DrawColorPixel(200,0,DRIVER_COLOR_BLACK);
            display.EPD_DisplayPart();
            assert(panelMock.data[0x26][0]==0xff);
            assert(panelMock.data[0x24][0]==0x7f && panelMock.data[0x24][4999]==0xfe);
            assert(panelMock.data[0x22][0]==0xcf);
            display.EPD_Clear();display.EPD_DisplayPart();
            assert(panelMock.data[0x26][0]==0x7f && panelMock.data[0x24][0]==0xff);
            display.EPD_PowerOff();assert(panelMock.data[0x22][0]==3);
            assert(display.healthy() && panelMock.levels[11]==1);
        }
        clean(); // Includes successful destruction and re-entry.
    }
    {
        epaper_driver_display display(200,200,config);
        display.EPD_DrawColorPixel(0,0,DRIVER_COLOR_BLACK);
        display.EPD_DisplayPart(); // No retained frame: establish a real full baseline.
        assert(display.healthy() && panelMock.data[0x24][0]==0x7f);
        assert(panelMock.data[0x26][0]==0x7f && panelMock.data[0x22][0]==3);
    }
    clean();
    for(int transfer=1;transfer<=7;++transfer) {
        {
            epaper_driver_display display(200,200,config);
            display.EPD_Init();display.EPD_Display();display.EPD_Init_Partial();
            panelMock.failTransfer=panelMock.transmits+transfer;
            display.EPD_DisplayPart();assert(!display.healthy() && panelMock.levels[11]==1);
            const int stopped=panelMock.transmits;
            display.EPD_Init();display.EPD_DisplayPart();display.EPD_PowerOff();
            assert(panelMock.transmits==stopped);
        }
        clean();
    }
    {
        epaper_driver_display display(200,200,config);
        panelMock.busy=true;
        const uint32_t started=panelMock.tick;
        display.EPD_Init();
        assert(!display.healthy() && panelMock.tick-started==5120);
        assert(panelMock.commands.empty());
    }
    clean();
    puts("PASS: actual V2 adapter init/full/partial/standby, frame history, allocation/GPIO/SPI/BUSY faults and teardown/re-entry");
}
