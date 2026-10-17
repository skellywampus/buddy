# Third-party material

Buddy's application, artwork, and creator-supplied sounds are MIT-licensed as
described in [LICENSE-SCOPE.md](LICENSE-SCOPE.md). This does not relicense
third-party files or dependencies. Retain their notices and follow their own
terms when using or distributing those parts.

Exact versions and linked components for release `2026101701` are recorded in
the [dependency inventory](releases/2026101701/compliance/dependency-inventory.json)
and [build information](releases/2026101701/build-info.json).

| Material | Terms and location |
| --- | --- |
| Pocket Scout hardware/sensor helpers | MIT; attribution and grant in `LICENSES/Pocket-Scout-MIT.txt`. |
| Waveshare generic V2 waveform/reference | MIT-style permission retained in `LICENSES/Waveshare-epd1in54-V2-permission.txt`; upstream source excerpt and provenance are under `third_party/waveshare_epaper_display/`. |
| Buddy-authored display adapter, firmware, tests, tools, documentation, and original creative assets | MIT under the root `LICENSE`; third-party portions remain separately identified here. |
| Adapted Espressif speech worker and header | Unlicense OR CC0-1.0, as stated in the source headers. |
| Arduino-ESP32 3.3.12 core, FS, Preferences, SD_MMC, Update, and Wire | LGPL-2.1-or-later with file-specific exceptions; corresponding source and notices in release compliance materials. |
| ESP-IDF 5.5.5 and selected SDK components | Apache-2.0 and component-specific permissive terms; pinned notices in the release compliance package. |
| FreeRTOS, FatFs, lwIP, mbedTLS, cJSON, SPIFFS, TLSF, HTTP parser, and micro-ecc | Upstream MIT/BSD/Apache-style grants and notices in the release compliance package. |
| ESP-SR 2.5.3 and supplied models/binary libraries | Espressif license terms restricted to Espressif products; preserved as provided. Some model/library internals are not source-available. |
| ESP-DL 3.3.11, dl_fft 0.7.0, and esp_diagnostics 1.3.4 | Version-specific grants and notices in the release compliance package. |
| Espressif radio/PHY/coexistence libraries | Espressif component terms in release notices. Their presence does not enable wireless Cable Friends. |
| GCC/libstdc++ runtime, Newlib, and Xtensa runtime/support | Version-specific runtime licenses and notices in the release compliance package. |

The release inventory is a conservative record of linked archive inputs,
including sections later discarded, and identifies each selected archive by
SHA-256. Component source commits follow the SDK pins. The corresponding
Arduino source archive and notices are supplied with the release.

## Binary distribution and relinking

Keep the release compliance directory, corresponding source, and notices with
each firmware download or assembled-device distribution. The root source tree
does not replace the exact library versions and notices identified for a given
binary release. The Arduino source archive is carried as verified numbered
pieces with an offline assembly helper; pinned notices are preserved with
their original paths.

The build also uses Espressif-supplied binary components. Their own grants are
retained rather than relabeled MIT. ESP-SR permits use on Espressif hardware,
which includes the supported board; this repository does not claim every SDK
model or binary is open source.

## Host flashing tools

The launchers install `esptool==5.3.0` (GPL-2.0-or-later) and `pyserial==3.5`
(BSD-3-Clause) into a private Python environment. These packages are fetched
from their distributors and are not bundled here; their licenses remain with
those installations. The Buddy installer and USB utilities are MIT-licensed
under the root license.

No Unidex source is used. The original Waveshare board BSP is not included;
the project has a separately documented display port and licensed generic
reference material.
