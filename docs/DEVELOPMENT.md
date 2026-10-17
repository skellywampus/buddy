# Developer guide

## Reference build

Use Arduino-ESP32 **3.3.12**, Arduino CLI, Python 3, and the checked-in custom partition table. The target is the V2 monochrome non-touch board with 8 MB flash and 8 MB OPI PSRAM. Shared hardware and sensor helpers are kept beside the Buddy sketch and host tests in `firmware/Buddy`.

```sh
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=custom,PSRAM=opi,FlashMode=qio,FlashSize=8M,USBMode=hwcdc,UploadMode=default,UploadSpeed=921600,CPUFreq=240,DebugLevel=none,EraseFlash=none' \
  --build-path /tmp/buddy-build firmware/Buddy
```

The matching ESP-SR English model is shipped by that core as `esp_sr/srmodels.bin` in its ESP32-S3 SDK package. Its expected SHA-256 is `c7ea01954baaa1fa89ef6b88ba8819edcbf419b7aba1e093950f8badf1a6c328`. The application verifies the model before command recognition. A model mismatch must not be “fixed” by deleting the verification; source changes need an intentional model/version update.

The actual application slot is **0x250000 bytes**, even if Arduino CLI prints a larger generic maximum. Package generation enforces the real limit. A fresh build can differ from an archived binary because build timestamps and paths can differ; do not claim byte-for-byte reproducibility unless measured.

The staged release additionally remaps development paths with `-ffile-prefix-map` and `-fmacro-prefix-map`. Supply those via `compiler.c.extra_flags` and `compiler.cpp.extra_flags`, preserving the FQBN's standard definitions. Do **not** replace `build.extra_flags`: that would remove the board's PSRAM, USB and core definitions. Path sanitization is not a firmware feature change, and its independently matched hashes live in the staged manifest.

## Package an update

For the complete matched release, use `python3 tools/build_release.py`, then `python3 tools/prepare_compliance.py`. These build/package tools never flash hardware. See [REBUILDING.md](REBUILDING.md) for the supplied library-source and modification route.

`firmware/Buddy/buddy_update_format.h` is the release-number source. Increment it for a changed public firmware image, then compile and package the same source:

```sh
python3 firmware/Buddy/package_update.py /tmp/buddy-build/Buddy.ino.bin /tmp/BUDDY.UPD
```

This creates a 68-byte header followed by the application image. It includes board identity, release, size and a SHA-256 digest. It rejects unmarked, mismatched, wrong-chip or oversized images. Packages are integrity-checked but **not cryptographically signed**: only install firmware from a trusted source.

The archived release includes first-install files, but ordinary updates use only `BUDDY.UPD`. Bootloader, partition table, OTA initialization and model must not be rewritten for a normal life-preserving update.

## Host verification

Install Clang with AddressSanitizer/UndefinedBehaviorSanitizer, Python 3 and ffmpeg. Then:

```sh
bash firmware/Buddy/tests/run_host_tests.sh
```

This runner is portable to macOS/Linux and uses temporary directories. It executes production and accelerated simulations, clock, runtime worker, the actual display driver with peripheral mocks, OTA acceptance, climate and power tests, Python asset/format/contracts checks, cross-platform installer/update tests, and the public bundle/source/compliance verifier. It never opens real USB hardware.

The host suite covers 24 lifetimes and 20 consecutive autonomous generations, but it is not an exhaustive proof of firmware or hardware safety. GPIO sequencing, standby rails, cable interoperability, battery-only startup and physical refresh behavior require actual board tests. The GitHub workflow currently performs host verification only, not a hosted ESP32 build or board test.

## Architecture

| Area | Main source |
| --- | --- |
| Life, needs, development and succession | `buddy_types.h`, `buddy_sim.inc`, `buddy_world.inc` |
| Inner life, routines, ambitions and inherited works | `buddy_inner_life.h`, `buddy_ambition.*`, `buddy_legacy.*` |
| House lifecycle | `buddy_house.*` |
| First-start influence and appearance | `buddy_intro.*`, `buddy_ui.inc`, `buddy_item_art.h`, `buddy_activity_art.inc` |
| Save loading, repair and whole-hour transactions | `buddy_storage.inc`, `buddy_commit.*`, recovery policies |
| Buttons, services and deadlines | `buddy_app.inc`, `buddy_button.h`, `buddy_deadlines.h` |
| Battery latch, sampling and power policy | `buddy_power_setup.inc`, `buddy_power.inc`, `buddy_battery_sample.inc`, `buddy_runtime_policy.h` |
| Speech, listening windows and audio | `buddy_mic.inc`, `buddy_speech.inc`, `buddy_esp32_sr.c`, `buddy_audio.inc` |
| Cable handshake, transport, encounters and cleanup | `buddy_cable*`, `buddy_nearby*` |
| Display transport and refresh | `third_party/waveshare_epaper_display/` |
| SD package acceptance and rollback | `buddy_update.inc`, `buddy_update_format.h`, `package_update.py` |

Visible moments and simulation accounting are separate. Changing the screen does not create elapsed life hours. Sound selection has its own random stream and does not consume the persistent life RNG. USB-host detection and charge-only trend estimates are not interchangeable electrical facts.

## Save invariants

- Keep life, settings and world state at the established NVS partition.
- Preserve frozen structure sizes and old readers unless a tested versioned migration replaces them.
- Never regenerate a saved seed, birth, lifespan or generation during an update.
- Alternating checksummed records retain a previous usable checkpoint.
- The whole-hour `aim-commit` journal reconciles changed life/house/ambition/site records, including ordinary progress, before later simulation can supersede them.
- Save the complete hourly afterimage only after its cursor and consequences are final; failed journal creation rolls the hour back.
- A pending or corrupt transaction must not silently create a different Buddy.
- Preserve independent settings such as Sound OFF when replaying a pending life image.
- The optional SD card is not the authoritative active save. Verify archive exports before removing internal copies.

## Hardware invariants

GPIO17 is **BAT_Control**, not an ADC-divider enable. Assert it before slow boot work and retain HIGH during normal operation. ADC measurements must never lower it. Manual/critical shutdown is the only intentional LOW path. Battery-only tests must release PWR and run beyond the next minute sampling deadline; USB-powered tests cannot prove battery-latch correctness.

Cable Friends temporarily takes over native USB data pins. Preserve one authoritative cleanup path and restart-based restoration; a meeting exit must not accidentally become a sleep gesture. Never test against a charger or computer while Cable Friends is active. No host mock can verify an arbitrary USB-C cable's wiring or prevent all back-powering.

## Sound assets

The eight source WAVs are included under `firmware/Buddy/sounds`. Regenerate the ADPCM header with:

```sh
python3 firmware/Buddy/build_sounds.py
```

This requires ffmpeg. The converter validates source format and the trimmed silent tail; generated clips are 16 kHz mono. Keep generated data and source audio together and run the sound fidelity test after a change. Do not change life RNG behavior while choosing sounds.

## Release checklist

Cross-platform first-flash safety tests run with `python3 -m unittest discover -s installer/tests -p 'test_*.py'` after installing `installer/requirements.txt`. The CI matrix covers Windows, Mac and Linux; it never flashes hardware. Native Windows serial API smoke checks run only on Windows. The full sanitized runner also runs installer tests and offline bundle validation. Keep the three root launchers and shared bootstrap/engine together. Automatic setup requires Python 3.10+ and internet once; no global Python packages are changed. See [first-flash instructions](../FIRST-FLASH.md).

Run host tests; compile production; generate the package; update release number, image hashes and manifest together; compare root/archive `BUDDY.UPD`; verify every matched image; update the changelog and documentation; and perform proportionate hardware tests without erasing a real Buddy. Finish the licensing/publication gates before distributing the full bundle. Do not ship an accelerated-life build as production.
