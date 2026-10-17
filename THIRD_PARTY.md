# Third-party material

Buddy's firmware is freeware under [LICENSE](LICENSE), not MIT/open source. The utilities are separately MIT licensed; see [INSTALLER-TOOLS-LICENSE](INSTALLER-TOOLS-LICENSE). The following dependencies retain their own terms. Exact versions and commits for release 2026101701 are recorded in its [release inventory](releases/2026101701/compliance/dependency-inventory.json) and [build information](releases/2026101701/build-info.json).

| Material | Terms and accompanying material |
| --- | --- |
| Pocket Scout hardware/sensor helpers | MIT; copyright and grant in LICENSES/Pocket-Scout-MIT.txt |
| Waveshare generic 1.54-inch V2 waveform/reference | MIT-style header grant; the applicable permission and attribution are included in [LICENSES](LICENSES/README.md) and the release notices. The implementation source is not part of this freeware package. |
| New display adapter, frame helpers and bounded waits | Buddy-owned firmware; freeware terms in LICENSE. Not third-party MIT. |
| Adapted Espressif speech worker | Unlicense OR CC0-1.0, retained in buddy_esp32_sr.c |
| Arduino-ESP32 3.3.12 core, FS, Preferences, SD_MMC, Update and Wire | LGPL-2.1-or-later with per-file exceptions; full corresponding source archive, matching app objects and tested relink workflow in releases/2026101701/compliance |
| ESP-IDF 5.5.5 and selected SDK components | Apache-2.0 and component-specific permissive terms; pinned notices in releases/2026101701/compliance/notices.zip in the public download |
| FreeRTOS, FatFs, lwIP, mbedTLS, cJSON, SPIFFS, TLSF, HTTP parser and micro-ecc | Their upstream MIT/BSD/Apache-style grants and retained notices are included in the same notice collection |
| ESP-SR 2.5.3 and its supplied models/binary libraries | Espressif MIT-style grant restricted to Espressif products; retain that exact license. Binary library/model internals are not all source-available |
| ESP-DL 3.3.11, dl_fft 0.7.0, esp_diagnostics 1.3.4 | Version-specific grants and notices in the release collection |
| Espressif radio/PHY/coexistence libraries selected by SDK linking | Espressif component licenses in the release notices; inclusion in a link does not enable wireless Cable Friends |
| GCC/libstdc++ runtime | GPL runtime terms with GCC Runtime Library Exception 3.1; accompanying exact toolchain notices |
| Newlib and Xtensa runtime/support | Version-specific license/copyright notices included with the release |
| Original WAVs, ADPCM and artwork routines | Created for Buddy; proprietary Buddy Freeware License. Raw sources are excluded. |

The release inventory lists actual archive members selected by the linker, including sections later discarded. It is a conservative record of linked inputs, not an assertion that every SDK feature is active. Each selected archive is identified by SHA-256. Component source commits follow the SDK's versions.txt and the ESP-IDF 5.5.5 submodule pins. The Arduino source was compared against all 198 installed core/library source files used by the reference build.

## Binary distribution and relinking

Keep the compliance directory, source archive, notices and [NOTICE.md](NOTICE.md)
with each firmware download and assembled-device distribution. Providing only
BUDDY.UPD omits required accompanying materials.

The public package includes the complete corresponding Arduino source as four
numbered archive pieces plus `assemble_source.py` and `source-parts.json`.
Reassembly checks every piece and the original archive hash, without network
access. All pinned notices are retained with their original paths inside
`notices.zip`. These transport changes let the entire repository fit one
GitHub browser upload without omitting required material. The source assembly
helper is separately MIT licensed under INSTALLER-TOOLS-LICENSE.

Buddy's application source is intentionally not included in the freeware
package. The LGPL route therefore depends on the release including complete
application object files, link configuration/data and tested instructions that
let recipients modify an LGPL-covered Arduino library and relink the firmware.
The LGPL source archive and license alone do not suffice. Release 2026101701
includes the exact application object files, versioned link helper and a test
report for a clean relink and a modified LGPL Preferences library. See
[relinking instructions](docs/REBUILDING.md) and the release compliance directory.

The build also uses Espressif-supplied binary components. Their grants are retained rather than relabeled MIT. ESP-SR permits use on Espressif hardware, which includes the supported board. This repository does not claim that every SDK/model binary is open source.

## Host flashing tools

The launchers install `esptool==5.3.0` (GPL-2.0-or-later) and `pyserial==3.5` (BSD-3-Clause) into a private Python environment. The packages are downloaded from their distributor and are not bundled into the repository or firmware. Their licenses remain with those installations. The first-flash and USB update/restore utilities are separately MIT licensed under INSTALLER-TOOLS-LICENSE. A preinstalled Python environment should not be added to a release.

Unidex source is not used; no permission from its author is required for this implementation. The original Waveshare board BSP and earlier firmware images are not part of this distribution.
