# Display source provenance

The adapter in this directory is a new ESP-IDF port written for Buddy's existing public interface. The earlier board BSP implementation was replaced in release 2026101601. Its source file and private Git history are not part of the public export.

The controller reference is Waveshare's generic Arduino `epd1in54_V2.cpp`, version V1.0 (2019-06-24), at commit `a794fbc39656b0f93938d1ffb3fdc77eaed9e9fc`:
https://github.com/waveshareteam/e-Paper/blob/a794fbc39656b0f93938d1ffb3fdc77eaed9e9fc/Arduino/epd1in54_V2/epd1in54_V2.cpp

The unmodified reference is preserved as `upstream/epd1in54_V2.cpp.txt`, SHA-256 `6a28e4c77c07a8a143ff971146a31ef03e9727c4b0a5badfa58f2da06c2efed6`. It contains the express MIT-style grant and author attribution.

| Material | Provenance |
| --- | --- |
| Full and partial waveform arrays in epaper_waveforms.h | Copied from that pinned reference, made const, with the complete upstream notice |
| Full initialization and update register values | Port of HDirInit, SetLut, DisplayFrame and DisplayPartFrame in that reference |
| Partial initialization register values | Port of SetFrameMemoryPartial in that reference |
| ESP-IDF transport, ownership, frame storage, fault handling and public API | Newly authored Buddy code, MIT |
| Prior-frame bank transfer, RAM-retaining standby, frame helpers and bounded waits | Buddy-owned additions, MIT; behavior preserved in the new implementation |

The adapter retains the tested board pin map and conservative 50/20/50 ms reset timing. It uses the documented 200 x 200 geometry only. The partial path sends the previous frame to RAM bank 0x26 and the new frame to bank 0x24, and only changes its software baseline after a successful update. A missing baseline triggers a full update before partial operation.

No Unidex code was copied. No redistribution grant is claimed for the original Waveshare board BSP. That BSP is unnecessary for this release because this directory contains the new port and the separately licensed reference instead.

The host driver test compiles the actual adapter against fake ESP-IDF peripherals and checks initialization, full/partial transfers, retained frames, standby, faults and resource teardown. These checks do not measure visible ghosting or panel power consumption; those require a real display.
