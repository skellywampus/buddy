#!/usr/bin/env python3
"""First USB install for a new Waveshare ESP32-S3-ePaper-1.54 V2 Buddy."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import tempfile
import time
from datetime import datetime
sys.dont_write_bytecode = True
from bundle_release import release_folder
from private_storage import backup_directory


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "installer"
SOURCE_VERSION = ROOT / "firmware" / "Buddy" / "buddy_update_format.h"
if (INSTALLER / "manifest.json").is_file():
    IMAGES, MANIFEST = INSTALLER / "images", INSTALLER / "manifest.json"
else:
    try:
        release_dir = release_folder(ROOT)
    except (OSError, ValueError):
        release_dir = ROOT / "releases" / "invalid-bundle"
    IMAGES, MANIFEST = release_dir / "first-install", release_dir / "manifest.json"
BACKUPS = backup_directory()
BUDDY_MARKER = b"BUDDY:WS154V2-BW:"
NVS_START, NVS_END = 0x9000, 0xE000
EXPECTED_ADDRESSES = (0x0, 0x8000, 0xE000, 0x10000, 0x4B0000)
EXPECTED_MAX_SIZES = (0x8000, 0x1000, 0x2000, 0x250000, 0x340000)
EXPECTED_FILES = ("bootloader.bin", "partitions.bin", "boot_app0.bin", "Buddy.ino.bin", "srmodels.bin")
BACKUP_BAUDS = (460800, 115200)


class InstallError(Exception):
    pass


def verify_bundle():
    try:
        manifest = json.loads(MANIFEST.read_text())
    except (OSError, ValueError) as exc:
        raise InstallError("Missing or damaged installer manifest.") from exc
    if not isinstance(manifest, dict) or manifest.get("flash_size") != 0x800000:
        raise InstallError("Installer manifest has the wrong flash size.")
    if type(manifest.get("release")) is not int or manifest["release"] <= 0:
        raise InstallError("Installer manifest has an invalid release number.")
    if SOURCE_VERSION.is_file():
        version = re.search(r"^#define BUDDY_FIRMWARE_VERSION (\d+)UL$", SOURCE_VERSION.read_text(), re.M)
        if not version or int(version.group(1)) != manifest["release"]:
            raise InstallError("Bundled firmware source and installer release do not match.")
    entries = manifest.get("images", [])
    if not isinstance(entries, list) or len(entries) != 5 or any(not isinstance(x, dict) for x in entries):
        raise InstallError("Installer manifest must contain exactly five images.")
    if tuple(item.get("file") for item in entries) != EXPECTED_FILES:
        raise InstallError("Installer manifest has unexpected image filenames.")
    try:
        addresses = tuple(int(item["address"], 0) for item in entries)
    except (ValueError, TypeError, KeyError) as exc:
        raise InstallError("Installer manifest has invalid flash offsets.") from exc
    if addresses != EXPECTED_ADDRESSES:
        raise InstallError("Installer manifest has unexpected flash offsets.")
    if tuple(item.get("max_size") for item in entries) != EXPECTED_MAX_SIZES:
        raise InstallError("Installer manifest has unsafe image partition sizes.")
    paths = []
    for item in entries:
        name = item["file"]
        if Path(name).name != name or not name.endswith(".bin"):
            raise InstallError("Installer manifest contains an invalid image path.")
        path = IMAGES / name
        if not path.is_file():
            raise InstallError(f"Missing {path}. Keep the installer folder together.")
        data = path.read_bytes()
        if not data or len(data) > item["max_size"]:
            raise InstallError(f"Unexpected size for {name}.")
        if hashlib.sha256(data).hexdigest() != item.get("sha256"):
            raise InstallError(f"Checksum mismatch for {name}; obtain a fresh installer.")
        paths.append(path)
    app = paths[3].read_bytes()
    marker = BUDDY_MARKER + str(manifest["release"]).encode() + b"UL"
    if app[:1] != b"\xe9" or app[12:14] != b"\x09\x00" or marker not in app:
        raise InstallError("The application image does not match this Buddy release.")
    update = ROOT / "BUDDY.UPD"
    if update.is_file():
        header = struct.Struct("<8sI16sII32s")
        data = update.read_bytes()
        expected = header.pack(b"BUDDYUP1", 1, b"WS154V2-BW\0\0\0\0\0\0",
                               manifest["release"], len(app), hashlib.sha256(app).digest())
        if data != expected + app:
            raise InstallError("BUDDY.UPD and the first-install images do not match.")
    # esptool's DIO/80MHz/8MB write settings must not change the expected
    # bootloader bytes; independent readback checks the exact bundled image.
    boot = paths[0].read_bytes()
    if boot[0] != 0xe9 or not 1 <= boot[1] <= 16 or boot[2:4] != b"\x02\x3f":
        raise InstallError("Bootloader flash configuration does not match this installer.")
    return manifest, paths


def esptool_command():
    if importlib.util.find_spec("esptool"):
        return [sys.executable, "-m", "esptool"]
    executable = shutil.which("esptool")
    if executable:
        return [executable]
    raise InstallError(
        "esptool is missing. Install it once with: python3 -m pip install esptool pyserial"
    )


def discover_port(timeout=75):
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise InstallError(
            "USB detection needs pyserial. Install it once with: python3 -m pip install pyserial"
        ) from exc
    deadline = time.monotonic() + timeout
    announced = False
    while True:
        candidates = sorted({p.device for p in list_ports.comports()
            if p.vid == 0x303A and
            (p.device.startswith(("/dev/cu.", "/dev/ttyACM", "/dev/ttyUSB")) or
             re.fullmatch(r"COM\d+", p.device, re.I))})
        if len(candidates) == 1:
            return candidates[0]
        if len(candidates) > 1:
            raise InstallError("More than one possible board is connected. Unplug the others or use --port. "
                               + ", ".join(candidates))
        if time.monotonic() >= deadline:
            raise InstallError("No ESP32 USB port found. Use a data-capable cable, close serial monitors, "
                "and try holding BOOT while pressing/releasing PWR, then release BOOT. "
                "On Linux, check serial-device permissions (often the dialout group); do not run the tool as root.")
        if not announced:
            print("Waiting up to 75 seconds for one board. Connect it directly with a data-capable USB-C cable…",flush=True)
            announced = True
        time.sleep(1)


def run_esptool(base, port, operation, *arguments, capture=False, baud=460800, after="no-reset"):
    command = base + ["--chip", "esp32s3", "--port", port, "--baud", str(baud),
                      "--after", after,
                      operation, *map(str, arguments)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None, check=False)
    if result.returncode:
        detail = (" Flash may be incomplete; keep the backup and rerun only after "
                  "checking the board." if operation == "write-flash" else "")
        raise InstallError(f"esptool {operation} failed.{detail}"
                           + ("\n" + result.stdout if capture else ""))
    return result.stdout if capture else ""


def validate_hardware(output):
    if not re.search(r"Chip type:\s+ESP32-S3-PICO-1\b", output):
        raise InstallError("This is not the required ESP32-S3-PICO-1 board module.")
    if not re.search(r"Embedded Flash 8MB\b", output):
        raise InstallError("The board does not report 8 MB embedded flash.")
    if not re.search(r"Embedded PSRAM 8MB\b", output):
        raise InstallError("The board does not report 8 MB embedded PSRAM.")
    size = re.search(r"(?:Detected flash size|Flash size):\s*(\d+)MB\b", output)
    if size and int(size.group(1)) != 8:
        raise InstallError("The flash chip size is not 8 MB.")
    mac = re.search(r"MAC:\s*([0-9a-f:]{17})", output, re.IGNORECASE)
    if not mac:
        raise InstallError("Could not read the board identity; refusing to flash.")
    return mac.group(1).replace(":", "").lower()


def validate_security(output):
    if "Secure Boot: Disabled" not in output or "Flash Encryption: Disabled" not in output:
        raise InstallError("This board has enabled or unknown flash security; refusing a first install.")


def already_buddy(flash):
    nvs = flash[NVS_START:NVS_END]
    return BUDDY_MARKER in flash or b"buddy-life\x00" in nvs or \
        b"life-a\x00" in nvs or b"life-b\x00" in nvs


def backup_flash(base, port, mac):
    BACKUPS.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(BACKUPS).free < 32 * 1024 * 1024:
        raise InstallError("Not enough free disk space for the full backup; no firmware was written.")
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    path = BACKUPS / f"first-install-{mac}-{stamp}.bin"
    if path.exists():
        raise InstallError(f"Backup already exists: {path}")
    print("Saving a complete read-only backup of this board's 8 MB flash…", flush=True)
    successful_baud = None
    for baud in BACKUP_BAUDS:
        partial = path.with_name(f"{path.name}.{baud}.partial")
        if partial.exists():
            raise InstallError(f"A previous partial backup already exists: {partial}")
        print(f"Reading at {baud} baud…", flush=True)
        try:
            run_esptool(base, port, "read-flash", "0x0", "0x800000", partial, baud=baud)
        except InstallError:
            print(f"Read at {baud} baud was interrupted; no firmware was written.", flush=True)
            continue
        if not partial.is_file() or partial.stat().st_size != 0x800000:
            print(f"Read at {baud} baud was incomplete; no firmware was written.", flush=True)
            continue
        partial.rename(path)
        successful_baud = baud
        break
    if successful_baud is None:
        raise InstallError(
            "Could not obtain a complete 8 MB backup at either USB speed; no firmware was written. "
            "Try another data-capable cable or USB port without a hub, then rerun. "
            "Any .partial files are incomplete and must not be used for restoration."
        )
    flash = path.read_bytes()
    digest = hashlib.sha256(flash).hexdigest()
    print(f"Backup: {path}\nSHA-256: {digest}", flush=True)
    if already_buddy(flash):
        raise InstallError(
            "This board appears to contain Buddy already. Refusing to replace an existing life. "
            "Use BUDDY.UPD for ordinary updates."
        )
    return path, successful_baud


def verify_written_images(base, port, baud, manifest, paths):
    print("Independently reading back all five written images…", flush=True)
    for item, source in zip(manifest["images"], paths):
        expected = source.read_bytes()
        verified = False
        with tempfile.TemporaryDirectory(prefix="buddy-first-readback-") as folder:
            readback = Path(folder) / "image.bin"
            for rate in (baud, 115200):
                try:
                    run_esptool(base, port, "read-flash", item["address"], hex(len(expected)), readback, baud=rate)
                    if readback.read_bytes() == expected:
                        verified = True
                        break
                except (InstallError, OSError):
                    pass
        if not verified:
            raise InstallError(f"Read-back verification failed for {item['file']}. Keep the backup; "
                               "flash may be incomplete. No full-chip erase was performed.")
        print(f"Verified {item['file']}", flush=True)


def wait_for_first_boot(port, release, timeout=180):
    # First-install boot_app0 uses UNDEFINED, not NEW/PENDING, OTA state.
    # Therefore v's VALID-only update acknowledgement can legitimately be 0.
    # Require the complete normal status instead, including WELCOME readiness.
    from usb_diagnostics import open_diagnostic
    prefixes = ("FIRMWARE ", "BUDDY ", "HOME ", "AMBITION ", "LEGACY ",
                "CABLE ", "POWER_SERVICE ", "INTRO ", "RECOVERY ")
    replies = {}
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        connection = None
        try:
            connection = open_diagnostic(port)
            while time.monotonic() < deadline:
                replies.clear()
                connection.write(b"?")
                # Drain a complete report before sending another request.
                for _ in range(40):
                    if time.monotonic() >= deadline:
                        break
                    line = connection.readline().decode("utf-8",errors="replace").strip()
                    for prefix in prefixes:
                        if line.startswith(prefix):
                            replies[prefix.strip()] = line
                    if line.startswith("RECOVERY ") or not line:
                        break
                if first_boot_report_ready(replies, release):
                    return
                time.sleep(.5)
        except OSError:
            replies.clear()
            time.sleep(1)
        finally:
            if connection:
                connection.close()
    raise InstallError("Flash images verified, but normal startup was not confirmed. Keep the backup "
                       "and USB power; do not erase flash or repeatedly force resets. Check WELCOME and report this output.")


def first_boot_report_ready(replies, release):
    if replies.get("FIRMWARE") != f"FIRMWARE BUDDY:WS154V2-BW:{release}UL":
        return False
    def fields(name):
        return dict(re.findall(r"(\w+)=(\d+)", replies.get(name,"")))
    for name in ("BUDDY", "HOME", "AMBITION", "LEGACY", "CABLE", "INTRO"):
        state = fields(name)
        if state.get("save_ok") != "1" or state.get("recovery") != "0":
            return False
    state = fields("BUDDY")
    power = fields("POWER_SERVICE")
    intro = fields("INTRO")
    return (state.get("clock_ok") == "1" and power.get("battery_latch") == "1" and
            power.get("wake_ready") == "1" and fields("RECOVERY").get("wdt") == "1" and
            (intro.get("required") == "1" or (state.get("alive") == "1" and int(state.get("gen","0")) >= 1)))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Install Buddy on one new V2 board over USB")
    parser.add_argument("--port", help="USB serial port; normally detected automatically")
    parser.add_argument("--verify-files", action="store_true",
                        help="check the bundled images without connecting or flashing")
    args = parser.parse_args(argv)
    manifest, paths = verify_bundle()
    print(f"Buddy {manifest['release']} — first USB installation", flush=True)
    print(f"Target: {manifest['target']}", flush=True)
    if args.verify_files:
        print("All five bundled images passed their checksums. No board was touched.")
        return 0
    base = esptool_command()
    port = args.port or discover_port()
    print(f"Found USB port: {port}", flush=True)
    hardware = run_esptool(base, port, "flash-id", capture=True)
    mac = validate_hardware(hardware)
    print(f"Verified ESP32-S3-PICO-1, 8 MB flash/PSRAM, ID {mac}.", flush=True)
    validate_security(run_esptool(base, port, "get-security-info", capture=True))
    backup, reliable_baud = backup_flash(base, port, mac)
    print("\nUSB cannot identify the display. Confirm the PCB says V2 and this is the")
    print("non-touch, black-and-white 1.54-inch Waveshare board. Its factory")
    print("firmware will be replaced; the backup above can be used for recovery.")
    if input("Type V2 to install Buddy (anything else cancels): ").strip() != "V2":
        print("Cancelled. No firmware was written.")
        return 1
    if validate_hardware(run_esptool(base, port, "flash-id", capture=True)) != mac:
        raise InstallError("The board changed after backup. No firmware was written.")
    validate_security(run_esptool(base, port, "get-security-info", capture=True))
    print("\nInstalling bootloader, partition table, OTA setup, Buddy and speech model…",
          flush=True)
    arguments = ["--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB"]
    for item, path in zip(manifest["images"], paths):
        arguments += [item["address"], path]
    run_esptool(base, port, "write-flash", *arguments, baud=reliable_baud)
    verify_written_images(base, port, reliable_baud, manifest, paths)
    run_esptool(base, port, "read-mac", baud=reliable_baud, after="watchdog-reset")
    print("Checking Buddy responds after the full-system restart…",flush=True)
    wait_for_first_boot(port, manifest["release"])
    print("\nSUCCESS: five images read-back verified and Buddy's normal startup confirmed.")
    print(f"Keep your private recovery backup: {backup}")
    print("Follow WELCOME on the screen: click next, double-click previous, hold to select.")
    print("For later releases, copy BUDDY.UPD to a microSD card; do not rerun this installer.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InstallError, OSError, KeyboardInterrupt, EOFError) as exc:
        print(f"\nStopped: {exc}", file=sys.stderr)
        raise SystemExit(1)
