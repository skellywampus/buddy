# First installation over USB

For easy Windows, Mac or Linux installation, follow [FIRST-FLASH.md](FIRST-FLASH.md). The included launchers detect the board, set up pinned dependencies, back up its full flash, validate hardware/security, install all five images, independently read them back, restart and check normal startup. Python 3.10+ is required and the first run needs internet. No Arduino installation is needed.

This is only for a **brand-new supported board**. An existing Buddy uses the [microSD update procedure](UPDATE.md), not this image set. The tool refuses recognized Buddy firmware/life keys. Initial provisioning needs the bootloader, partition table, OTA initialization, application and speech model; the application `.bin` alone is insufficient. The details below are for maintainers using the underlying tools directly.

Verify V2, black-and-white, non-touch hardware, ESP32-S3-PICO-1, 8 MB flash and 8 MB PSRAM. Native USB cannot identify the attached display. Check secure-boot/encryption status; do not apply this unencrypted image set to a security-configured board. Connect exactly one board with a data-capable cable and keep Cable Friends OFF.

## Matched images for 2026101701

Files are under [releases/2026101701/first-install](releases/2026101701/first-install). Hashes are in [manifest.json](releases/2026101701/manifest.json). Existing lives can use the [USB updater](USB-UPDATE.md).

| Image | Flash offset |
| --- | --- |
| bootloader.bin | 0x0 |
| partitions.bin | 0x8000 |
| boot_app0.bin | 0xE000 |
| Buddy.ino.bin | 0x10000 |
| srmodels.bin | 0x4B0000 |

Verify the release using the supplied checksums and follow the platform-specific first-flash instructions in this folder. The complete source and verification scripts are also available in this repository.

Take and retain a complete 8 MB flash backup before the first write, verify its size and hash, and check whether the board already has a Buddy life. A partial read is not a backup. **Stop if this is an existing Buddy.** Do not erase the chip or write the life-save partition at 0x9000. Keep all five images from the same release.

After verifying the hardware, backup and files, an experienced user can use esptool's standard `write-flash` command with the five offset/image pairs above, DIO flash mode, 80 MHz flash frequency and 8 MB flash size. Use a reliable transfer speed. On ESP32-S3 native USB/JTAG, `--after watchdog-reset` performs the full-system restart needed to leave latched ROM download mode; a core-only RTS reset can leave the board waiting for download. See [Espressif's troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html#leaving-download-mode-in-usb-serial-jtag-mode).

Verify the written images and a functioning startup after the final restart, not only a success message from the flash write. Then complete WELCOME: one BOOT click advances, two go back and a hold selects. It asks for boy/girl appearance, a first keepsake and an initial spark. The owner can influence the start without controlling the rest of the life.

The launcher retains ROM mode through read-back and then uses `read-mac --after watchdog-reset` to start Buddy. Subsequent diagnostics preserve DTR/RTS and clear POSIX HUPCL. The complete status must show the right release, healthy saves/clock, battery latch, wake configuration, watchdog and WELCOME readiness. First-install OTA metadata is UNDEFINED, so startup does not depend on the existing updater's VALID-only acknowledgement.

Do not copy a first-install OTA selector onto a live Buddy to “fix” an update, restore an older life backup over a healthy current life, or use an accelerated-test image. For an existing life, use the [USB updater](USB-UPDATE.md) or the SD update method. Keep the complete matched release and its third-party license materials together; the root MIT license covers Buddy-owned work, while dependencies retain their own terms.
