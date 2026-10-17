# Release notes

## MIT source publication — October 17, 2026

Buddy-owned firmware source, tests, tools, documentation, artwork, and the
creator-supplied sound clips are now included under the MIT License. Updated
the export workflow to include the source and test suites, removed obsolete
freeware/source-private statements, and preserved all third-party licenses,
notices, and corresponding-source materials. The production firmware image,
saved-life format, and device behavior are unchanged; this repository update
was not flashed to hardware.

## 2026101701 — source-private freeware compliance package — October 17, 2026

Publication packaging: made the complete public tree fit one GitHub browser
upload (under 100 files, each under 25 MiB), without removing corresponding
source or notices. Added a verified offline source-assembly helper, archived
notices with their paths intact, checked HTML image links as well as Markdown,
and updated both illustrated Word manuals with readable GitHub editions.
Firmware images are unchanged.

USB tools now keep recovery backups/receipts in the user's private application
data directory and suppress Python bytecode caches, so using them cannot add
private board data to the publishable folder. Legacy restore receipts remain
supported.

Release-tool review: repaired USB install/update/restore discovery in the public
folder without firmware source; added actual export validation and regressions;
ensured Git includes the required object files; corrected the matching build's
archive inventory; hardened relink integrity/toolchain checks and image output;
bound relink acceptance to the exact artifacts; and aligned the documentation
and freeware permissions. The firmware images and saved-life format are unchanged.

No Buddy behavior or save layout changed. Rebuilt from the exact private source as a new release, retained the full corresponding Arduino-ESP32 source and notices, and added the exact application object files plus a relink helper. A clean relink reproduced the released image payload; a second relink with a deliberately modified LGPL Preferences implementation succeeded and changed the firmware payload. Host-side only; no board was flashed.

## 2026101601 — MIT-provenance display port — October 16, 2026

Replaced the remaining board display BSP implementation with a newly authored ESP-IDF port of the pinned MIT-licensed Waveshare V2 reference. Retained full/partial refresh, previous-frame transfer, bounded BUSY waits, standby and Buddy's public display API. Added executable tests of the actual adapter for initialization, transfers, retained frames, allocation/GPIO/SPI/BUSY faults and teardown/re-entry. Fixed invalid-width division in the frame helper. The simulation, existing save layout, controls and appearance are unchanged.

The release includes matched first-install images, BUDDY.UPD, library source, pinned SDK/runtime notices, linked archive hashes, build/rebuild tools and a clean public export tool. Added cross-platform USB update/previous-app restore launchers. Existing-life updates verify the partition layout, make a full backup, write the inactive slot and confirm the running app. Returning selects the preserved app without rewinding life data.

Sanitized host/installer/release tests and ESP32-S3 compilation pass. The owner reports that the firmware works as intended on hardware.

## Repository preparation — October 16, 2026

Consolidated the shared hardware and sensor helpers into `firmware/Buddy` and updated source/test include paths. Their Pocket Scout MIT attribution remains in LICENSES. The display provenance issue was addressed in 2026101601; the separate source-private LGPL relinking package is release 2026101701.

## First-flash tooling — October 14, 2026

Added Windows, Mac and Linux launchers with isolated dependency setup, automatic port detection, full backup, hardware/security checks, existing-life refusal, five-image independent read-back and full-system restart/startup verification. Added beginner instructions and cross-platform host-test CI. Firmware remains 2026101305; application images, save data and simulation are unchanged. Tools have local mocked transport/safety and sanitized regression coverage; actual Windows/Linux board flashing has not been tested in this development pass.

## 2026101305 — October 13, 2026

- Restores early assertion of GPIO17 / BAT_Control, the whole-board battery latch.
- Removes incorrect ADC-related latch toggling introduced in 2026101303.
- Preserves median battery monitoring, low-voltage safeguards, sensor cleanup and idle scheduling.
- Preserves all saved identities, ages, generations, history, art, controls and Cable Friends behavior.
- Battery-only responsiveness beyond 90 seconds and manual sleep/PWR wake were confirmed by the development-board owner. Other hardware combinations still require testing.

Staged privacy-remapped application (2,180,304 bytes), SHA-256: `caedc76662b32af5b5b4b002176f79017d5a512cb6fb87746410e3e08a3fb543`.

Staged SD package (2,180,372 bytes), SHA-256: `c2a84f787f1100a6a24ba7d9bbb81fbb07e373bdbf5b770abf774e358c121c95`.

This staged build removes local developer paths and was compiled/host-tested, not flashed. The physical checks above apply to the private development build of the same production source. Do not mix the staged hashes/images with the older private bundle. The actual application source and save behavior are unchanged.

## 2026101304 — October 13, 2026

Ordinary house, unfinished ambition and inherited-site hourly changes now recover alongside their matching life checkpoint. The existing whole-hour journal protects the matched consequences and cursor; permanent save layouts remain unchanged.

## 2026101303 — October 13, 2026

Adds independent minute battery checks, invalid-reading handling, low-cell audio guards, sensor failure backoff, display standby cleanup and stronger idle/wake safeguards. The GPIO17 divider interpretation in this release was wrong; its battery-only power regression is corrected in 2026101305.

## 2026101302 / 2026101301 — October 13, 2026

Stabilization of lifecycle/succession, save migration and rollback, hourly completion recovery, update acceptance/watchdog handling, clocks, display waits, microphone/worker teardown and routine development.

This is a curated public-facing changelog, not the private engineering history or incident log. The folder is prepared for review, not yet cleared/published as a public distribution.
