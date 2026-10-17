# Put Buddy on a new board

This installs complete Buddy firmware on one **brand-new Waveshare ESP32-S3-ePaper-1.54 V2, monochrome, non-touch** board. It is not an updater for an existing Buddy. For later updates use `BUDDY.UPD` on microSD; see [UPDATE.md](UPDATE.md).

## Before you start

1. Extract the **whole Buddy folder** to a writable local folder. Do not move a launcher out of that folder or run inside a ZIP. Keep the installer, release manifest, root `BUDDY.UPD`, and five matched images together. Buddy application source is not required or included.
2. Install **Python 3.10 or newer** once from [python.org](https://www.python.org/downloads/), if needed. On Windows, enable the Python launcher or “Add Python to PATH.” On Debian/Ubuntu, install `python3` and `python3-venv` through your package manager if missing.
3. Have internet access for the first run. The tool automatically installs pinned flashing dependencies in its own environment; it does not change system Python packages. Later runs reuse that environment. No Arduino installation is needed.
4. Connect **only one board**, directly to the computer with a data-capable USB-C cable. Close serial monitors and other flashing tools. Keep Cable Friends OFF.
5. Check the PCB's **V2** marking and black-and-white, non-touch display. V1, color and touch boards are unsupported. USB can verify the chip/memory but cannot identify the display.

## Run the tool

| Computer | Open this file |
| --- | --- |
| Windows | Double-click `FLASH-NEW-BUDDY.bat` |
| Mac | Open Terminal in the Buddy folder and run `bash FLASH-NEW-BUDDY.command` |
| Linux | Open a terminal in the Buddy folder and run `bash FLASH-NEW-BUDDY.sh` |

On Mac, if downloaded-file security prevents opening, use Finder's **Open** action and approve only a download you trust. Alternatively, open Terminal in the Buddy folder and run `bash FLASH-NEW-BUDDY.command`; this also works if executable permission was lost. Never disable security protections globally.

The tool checks release files, detects the port, verifies the required module/memory and disabled flash security, and saves a complete **8 MB backup**. It refuses recognized Buddy firmware or life-save keys. It then asks you to type `V2` to confirm the display. This is the only installation confirmation; inspect the board before answering.

Keep the cable connected until **SUCCESS** appears. The tool writes all five images, independently reads them back, performs the board's required full-system restart, and checks normal startup status: release, saves, clock, battery latch, wake configuration, watchdog and WELCOME readiness. This does not prove the physical screen works or test battery-only operation.

Follow **WELCOME** on screen: one BOOT click moves forward, two move back, a hold selects. No SD card is needed for installation. The tool prints the private backup's full path. Backups are outside the downloaded folder: `~/Library/Application Support/Buddy/backups` on Mac, `%LOCALAPPDATA%\Buddy\backups` on Windows, and `$XDG_DATA_HOME/Buddy/backups` or `~/.local/share/Buddy/backups` on Linux. Never upload them or restore them over another Buddy.

## If something stops

- **No board:** check the data cable, avoid hubs, close serial monitors and reconnect. If necessary, hold BOOT, press/release PWR, then release BOOT to enter download mode; rerun. Detection waits up to 75 seconds.
- **Multiple boards:** unplug the others. Experienced users may specify `python3 installer/launch_first_flash.py --port /dev/ttyACM0` (Windows: `py -3 installer\launch_first_flash.py --port COM3`). Do not guess the port.
- **Linux permission denied:** arrange serial-device access for your account, often the `dialout` group, then log out/in. Your distribution may differ. Do not run the flasher as root or grant world-write access.
- **Windows port missing:** use Windows Update/device-manager support for Espressif's native USB Serial/JTAG device. Do not install unrelated CH340 drivers or random driver packages. Port numbers can change after reconnecting.
- **Backup failure:** the tool retries at a lower speed and will not write without a complete backup. `.partial` files are not backups. Try a different cable/direct port. Reads may take several minutes; do not unplug mid-read.
- **Existing Buddy:** stop and use the SD updater. There is no force/erase override.
- **Cancelled before writing:** no firmware changed, but the board may remain in download mode. A brief PWR restart/reconnection normally returns the original firmware.
- **Write, read-back or startup failure:** installation may be incomplete, or Buddy may be running without confirmation. Retain the full backup and complete output. Do not erase the chip or repeatedly force resets. Ask an experienced maintainer to inspect it; this is not a recovery tool for a live Buddy.

## Offline download check

Run `python3 installer/launch_first_flash.py --verify-files` (Windows: `py -3 installer\launch_first_flash.py --verify-files`). All launchers accept this too. It checks the five images against the packaged release and SD update without installing packages or touching hardware.

The tool downloads `esptool 5.3.0` and `pyserial 3.5` from Python's package index. Its environment is cached under `%LOCALAPPDATA%\Buddy\first-flash` on Windows, `~/Library/Caches/Buddy/first-flash` on Mac, or `$XDG_CACHE_HOME/Buddy/first-flash` / `~/.cache/Buddy/first-flash` on Linux. An internet-free initial setup requires an experienced user to preinstall the pinned dependencies and run `installer/flash_new_buddy.py` directly.

See [FIRST-INSTALL.md](FIRST-INSTALL.md) for developer details. No full-chip erase or NVS life-save writes are performed. Software cannot guarantee recovery from cable/power loss or broken hardware.
