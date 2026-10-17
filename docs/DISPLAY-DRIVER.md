# Display driver

Release **2026101601** contains the ESP-IDF display adapter now used by the main development branch. It replaces the prior board-specific BSP with a newly authored implementation based on the pinned MIT-licensed generic Waveshare V2 reference. [Display provenance](../third_party/waveshare_epaper_display/PROVENANCE.md) records the reference, reused waveform/register material and original Buddy code.

The adapter preserves Buddy's display API, full and partial refresh behavior, previous-frame transfer, RAM-retaining standby and bounded BUSY waits. Transfers fail closed; frame history only advances after success. Buffers, SPI device and owned SPI bus have explicit lifetime handling. The owner reports that the firmware works as intended on hardware.

The host suite compiles the actual driver against mocked ESP-IDF peripherals. It checks full initialization, full/partial data, standby, missing-baseline recovery, allocation/GPIO/SPI/BUSY faults and teardown/re-entry. These tests do not measure long-term ghosting or panel power.

For an existing Buddy, use [USB-UPDATE.md](../USB-UPDATE.md). The updater backs up the board, writes only the inactive app and alternate OTA selector, and verifies startup. The paired restore command selects the preserved previous app without restoring old life data. First-install launchers are only for new boards.
