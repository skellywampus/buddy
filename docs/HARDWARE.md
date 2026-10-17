# Hardware and electrical boundaries

Target: **Waveshare ESP32-S3-ePaper-1.54 V2, monochrome, non-touch**, ESP32-S3-PICO-1-N8R8, 8 MB flash and 8 MB PSRAM. Check the PCB's V2 marking, not only the listing title. V1, touchscreen and 1.54G color firmware are not interchangeable with this release.

Manufacturer [board documentation](https://docs.waveshare.com/ESP32-S3-ePaper-1.54) and [hardware resources](https://docs.waveshare.com/ESP32-S3-ePaper-1.54/Resources-And-Documents) are the authority for physical dimensions, battery specifications and schematics. This project does not supply a verified enclosure drawing or certify a commercial assembled product.

## Pin assignments

| Function | GPIO/address |
| --- | --- |
| E-paper DC / CS / SCK / MOSI / RESET / BUSY | 10 / 11 / 12 / 13 / 9 / 8 |
| E-paper power control | 6 |
| Battery-to-system latch, BAT_Control | 17; HIGH in ordinary operation |
| Battery ADC input | 4 |
| BOOT interaction button / PWR button | 0 / 18 |
| Native USB D− / D+ | 19 / 20 |
| I²C SDA / SCL | 47 / 48 |
| PCF85063 RTC / SHTC3 / ES8311 codec | 0x51 / 0x70 / 0x18 |
| SD one-bit CLK / CMD / D0 | 39 / 41 / 40 |
| Audio power / amplifier | 42 / 46 |
| Audio MCLK / BCLK / DIN / DOUT / WS | 14 / 15 / 16 / 45 / 38 |
| Status LED | 3 |

Pin assignments, active levels, and shutdown order are implementation details and are not included in this end-user hardware overview. Do not assume that HIGH always means a peripheral is powered.

## Battery, sleep and charging

The battery percentage is a voltage-based estimate, not a fuel gauge or guaranteed remaining runtime. Unknown ADC readings display unknown rather than inventing a percentage. Firmware low-cell checks complement, but do not replace, battery/charger hardware protection. Use a suitable cell, connector polarity and charger arrangement per the manufacturer's documentation.

GPIO17 controls the whole battery supply path. Treating it as a switched ADC divider cuts battery-only operation. Release 2026101305 restores early latch assertion and ADC reads that leave the latch untouched. Intentional manual/critical shutdown disables the latch after state/storage work. Depending on supply conditions, the board can enter deep sleep or lose its battery-fed system rail; the persistent e-paper face is not proof of physical electrical isolation.

Auto rotation detects an active USB host directly, but charge-only detection uses a conservative voltage trend because charger status is not routed to the MCU. Set Normal or Flipped if the automatic estimate does not suit your stand or charger.

## Cable Friends is not USB

The firmware uses a low-speed, one-wire open-drain UART transport on native USB GPIO19, with GPIO20 reserved/released as part of the pair. This is a deliberate device-to-device use of a USB-C cable, not a standards-compliant USB host/device connection, VBUS negotiation or power-transfer feature.

1. Disconnect both units from computers, chargers, hubs and other devices.
2. Run both on their own batteries.
3. Enable Cable Friends on both before joining them.
4. Use only a suitable ordinary USB-C cable between those two Buddies.
5. End the meeting and exit the mode before connecting anything else.

Cable insertion and all charge-only sources cannot be sensed reliably by this hardware. Software cannot enforce the whole rule. Cable Friends has functional tests and actual two-device usage, but it is not certification that every cable, power path or misuse is electrically safe. Anyone commercializing an assembled device needs independent electrical, battery and product-safety assessment.

## SD handling

Manual/critical sleep closes files and unmounts the card before the safe-to-swap screen. Wait until the sleep transition has finished. Do not remove the card during normal awake operation, updates or archive writes. Unmounting does not guarantee that every slot contact is physically unpowered. Eject the card properly from the computer before reinserting it.

## Partition map

| Partition | Offset | Size |
| --- | --- | --- |
| NVS life/settings | 0x9000 | 0x5000 |
| OTA selectors | 0xE000 | 0x2000 |
| App slot 0 | 0x10000 | 0x250000 |
| App slot 1 | 0x260000 | 0x250000 |
| Speech model | 0x4B0000 | 0x340000 |
| Coredump | 0x7F0000 | 0x10000 |

Do not change this layout or erase a saved board as a routine firmware update.
