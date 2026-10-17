#!/usr/bin/env python3
"""Safely install a packaged Buddy OTA update to one V2 board over USB."""

import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from datetime import datetime
from pathlib import Path
sys.dont_write_bytecode = True
from bundle_release import release_folder
from private_storage import backup_directory, restore_receipt

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "BUDDY.UPD"
SOURCE_VERSION = ROOT / "firmware" / "Buddy" / "buddy_update_format.h"
try:
    RELEASE_FOLDER = release_folder(ROOT)
except (OSError, ValueError):
    RELEASE_FOLDER = ROOT / "releases" / "invalid-bundle"
BUNDLED_IMAGE = RELEASE_FOLDER / "first-install" / "Buddy.ino.bin"
INSTALLER_MANIFEST = RELEASE_FOLDER / "manifest.json"
BACKUPS = backup_directory()
FLASH_SIZE = 0x800000
OTA_DATA = 0xE000
OTA_SECTOR_SIZE = 0x1000
APP_OFFSETS = (0x10000, 0x260000)
APP_SIZE = 0x250000
TARGET = b"WS154V2-BW\0\0\0\0\0\0"
HEADER = struct.Struct("<8sI16sII32s")
OTA_ENTRY = struct.Struct("<I20sII")
MARKER_PREFIX = b"BUDDY:WS154V2-BW:"
BACKUP_BAUDS = (460800, 115200)


class UpdateError(Exception):
    pass


def parse_package(path):
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise UpdateError(f"Cannot read {path}: {exc}") from exc
    if len(data) < HEADER.size:
        raise UpdateError("BUDDY.UPD is incomplete.")
    magic, fmt, target, release, image_size, digest = HEADER.unpack_from(data)
    image = data[HEADER.size:]
    if magic != b"BUDDYUP1" or fmt != 1 or target != TARGET:
        raise UpdateError("This is not a Buddy V2 monochrome update package.")
    if not release or not image_size or image_size > APP_SIZE or len(image) != image_size:
        raise UpdateError("The update package has an invalid image size.")
    if image[:1] != b"\xe9" or image[12:14] != b"\x09\x00" or hashlib.sha256(image).digest() != digest:
        raise UpdateError("The firmware image is damaged or is not an ESP32-S3 app.")
    marker = MARKER_PREFIX + str(release).encode() + b"UL"
    if marker not in image:
        raise UpdateError("The firmware board/release marker does not match the package.")
    try:
        manifest = json.loads(INSTALLER_MANIFEST.read_text())
        expected = manifest["release"]
        if type(expected) is not int or expected <= 0:
            raise ValueError("Invalid manifest release")
        if SOURCE_VERSION.is_file():
            source = SOURCE_VERSION.read_text()
            expected = int(re.search(r"^#define BUDDY_FIRMWARE_VERSION (\d+)UL$", source, re.M).group(1))
        bundled_image = BUNDLED_IMAGE.read_bytes()
        image_entry = next(item for item in manifest["images"] if item["file"] == "Buddy.ino.bin")
        if hashlib.sha256(bundled_image).hexdigest() != image_entry["sha256"]:
            raise ValueError("Bundled application checksum mismatch")
    except (OSError, ValueError, AttributeError, TypeError, KeyError, StopIteration) as exc:
        raise UpdateError("The local firmware release files are incomplete; restore the Buddy folder.") from exc
    if release != expected or release != manifest.get("release") or image != bundled_image:
        raise UpdateError(
            "BUDDY.UPD does not match the current firmware bundle. Rebuild/copy the latest package before updating."
        )
    return release, image


def esptool_command():
    if importlib.util.find_spec("esptool"):
        return [sys.executable, "-m", "esptool"]
    executable = shutil.which("esptool")
    if executable:
        return [executable]
    raise UpdateError("esptool is missing. Install it with: python3 -m pip install esptool pyserial")


def discover_port(timeout=75):
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise UpdateError("USB detection needs pyserial. Install it with: python3 -m pip install pyserial") from exc
    deadline = time.monotonic() + timeout
    waiting = False
    while True:
        ports = sorted({p.device for p in list_ports.comports()
                 if p.vid == 0x303A and
                 (p.device.startswith(("/dev/cu.", "/dev/ttyACM", "/dev/ttyUSB")) or
                  re.fullmatch(r"COM\d+", p.device, re.I))})
        if len(ports) == 1:
            return ports[0]
        if len(ports) > 1:
            raise UpdateError("More than one ESP32 USB port is present. Unplug other boards or use --port: " +
                              ", ".join(ports))
        if time.monotonic() >= deadline:
            raise UpdateError("No ESP32 USB port found. Connect one Buddy with a data-capable cable, "
                              "leave Cable Friends OFF, and press BOOT to wake him.")
        if not waiting:
            print("Waiting up to 75 seconds for Buddy's USB port. Press BOOT to wake him sooner; "
                  "Cable Friends must be OFF.", flush=True)
            waiting = True
        time.sleep(1)


def run_esptool(base, port, operation, *args, baud=460800, capture=False,
                after="hard-reset"):
    command = base + ["--chip", "esp32s3", "--port", port, "--baud", str(baud),
                      "--after", after,
                      operation, *map(str, args)]
    result = subprocess.run(command, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None, check=False)
    if result.returncode:
        extra = ("\n" + result.stdout) if capture and result.stdout else ""
        raise UpdateError(f"esptool {operation} failed. Keep the full backup and inspect Buddy before retrying.{extra}")
    return result.stdout if capture else ""


def validate_hardware(output):
    if not re.search(r"Chip type:\s+ESP32-S3-PICO-1\b", output):
        raise UpdateError("The connected module is not an ESP32-S3-PICO-1.")
    if not re.search(r"Embedded Flash 8MB\b", output) or not re.search(r"Embedded PSRAM 8MB\b", output):
        raise UpdateError("The module must report 8 MB flash and 8 MB PSRAM.")
    mac = re.search(r"MAC:\s*([0-9a-f:]{17})", output, re.I)
    if not mac:
        raise UpdateError("Could not read the board identity; refusing to flash.")
    return mac.group(1).replace(":", "").lower()


def read_full_backup(base, port, mac):
    BACKUPS.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    final = BACKUPS / f"usb-update-{mac}-{stamp}.bin"
    if final.exists():
        raise UpdateError(f"Backup already exists: {final}")
    for baud in BACKUP_BAUDS:
        partial = final.with_name(final.name + f".{baud}.partial")
        if partial.exists():
            raise UpdateError(f"Previous incomplete backup exists: {partial}")
        print(f"Reading a complete 8 MB safety backup at {baud} baud…", flush=True)
        try:
            run_esptool(base, port, "read-flash", "0x0", hex(FLASH_SIZE), partial, baud=baud)
        except UpdateError:
            print("That read stopped; no firmware was written. Trying the slower USB speed…", flush=True)
            continue
        if partial.is_file() and partial.stat().st_size == FLASH_SIZE:
            partial.rename(final)
            digest = hashlib.sha256(final.read_bytes()).hexdigest()
            print(f"Full backup: {final}\nSHA-256: {digest}", flush=True)
            return final, baud
        print("That backup was incomplete; no firmware was written.", flush=True)
    raise UpdateError("Could not obtain a complete 8 MB backup. No firmware was written; try another cable/USB port.")


def ota_crc(sequence):
    # ESP-IDF esp_rom_crc32_le(UINT32_MAX, &ota_seq, sizeof(ota_seq)).
    return zlib.crc32(struct.pack("<I", sequence), 0xFFFFFFFF)


def parse_ota_entry(raw, sector):
    sequence, label, state, crc = OTA_ENTRY.unpack(raw[:OTA_ENTRY.size])
    if sequence in (0, 0xFFFFFFFF) or ota_crc(sequence) != crc:
        return None
    return {"sequence": sequence, "state": state, "sector": sector}


def seq_newer(a, b):
    return 0 < ((a - b) & 0xFFFFFFFF) < 0x80000000


def active_ota_entry(flash):
    entries = []
    for i in range(2):
        offset = OTA_DATA + i * OTA_SECTOR_SIZE
        entry = parse_ota_entry(flash[offset:offset + OTA_ENTRY.size], i)
        if entry:
            entries.append(entry)
    bootable = [entry for entry in entries if entry["state"] in (0, 1, 2, 0xFFFFFFFF)]
    if not bootable:
        raise UpdateError("Cannot identify a valid OTA boot slot; refusing to guess.")
    active = bootable[0]
    for entry in bootable[1:]:
        if seq_newer(entry["sequence"], active["sequence"]):
            active = entry
    if active["state"] == 1:  # ESP_OTA_IMG_PENDING_VERIFY
        raise UpdateError("The current app has not completed boot validation; let Buddy finish starting first.")
    latest = active
    for entry in entries:
        if seq_newer(entry["sequence"], latest["sequence"]):
            latest = entry
    return active, latest


def make_ota_entry(sequence):
    seq = struct.pack("<I", sequence)
    return OTA_ENTRY.pack(sequence, bytes(20), 0, ota_crc(sequence))


def selector_readback_matches(expected, actual, sequence):
    if len(actual) != len(expected) or len(actual) < OTA_ENTRY.size:
        return False
    read_sequence, _label, state, crc = OTA_ENTRY.unpack_from(actual)
    # The read command reconnects through the ROM bootloader and resets the
    # board. Only accept VALID here; accepting NEW could make that reset itself
    # trigger rollback before the app has confirmed the first boot.
    if read_sequence != sequence or state != 2 or crc != ota_crc(sequence):
        return False
    normalized = bytearray(actual)
    # ESP-IDF changes NEW to VALID during the first successful app boot.
    normalized[24:28] = bytes(4)
    return bytes(normalized) == expected


def prepare_update(flash, release):
    if len(flash) != FLASH_SIZE:
        raise UpdateError("The safety backup is not exactly 8 MB.")
    active, latest = active_ota_entry(flash)
    active_slot = (active["sequence"] - 1) % 2
    active_offset = APP_OFFSETS[active_slot]
    active_image = flash[active_offset:active_offset + APP_SIZE]
    current_markers = re.findall(MARKER_PREFIX + rb"(\d+)UL", active_image)
    if not current_markers:
        raise UpdateError("The selected app slot does not identify as Buddy V2; no write was made.")
    current_release = max(int(value) for value in current_markers)
    if release <= current_release:
        raise UpdateError(f"Package {release} is not newer than the installed Buddy {current_release}.")
    target_slot = 1 - active_slot
    sequence = latest["sequence"] + 1
    if sequence >= 0xFFFFFFFF:
        raise UpdateError("OTA sequence is at its rollover boundary; manual recovery is required.")
    if (sequence - 1) % 2 != target_slot:
        sequence += 1
    if sequence >= 0xFFFFFFFF:
        raise UpdateError("OTA sequence is at its rollover boundary; manual recovery is required.")
    selector_sector = 1 - active["sector"]
    selector = bytearray(b"\xff" * OTA_SECTOR_SIZE)
    selector[:OTA_ENTRY.size] = make_ota_entry(sequence)
    return current_release, active_slot, target_slot, sequence, selector_sector, bytes(selector)


def validation_serial():
    """Open-time line updates can reset native USB/JTAG, even with False values.

    Keep the reset lines untouched and suppress POSIX hangup-on-close. This
    channel is only for app diagnostics, never for ROM/flasher reset sequences.
    """
    import serial
    import os
    if os.name == "nt":
        from usb_diagnostics import WindowsDiagnostic
        class DeferredWindows:
            def open(self):
                self.connection = WindowsDiagnostic(self.port)
            def write(self, data):
                return self.connection.write(data)
            def readline(self):
                return self.connection.readline()
            def close(self):
                if hasattr(self, "connection"):
                    self.connection.close()
        return DeferredWindows()
    import termios

    class NoResetSerial(serial.Serial):
        def _update_dtr_state(self):
            pass

        def _update_rts_state(self):
            pass

        def _reconfigure_port(self, force_update=False):
            attrs = termios.tcgetattr(self.fd)
            attrs[2] &= ~termios.HUPCL
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            super()._reconfigure_port(force_update=force_update)

    return NoResetSerial()


def wait_for_boot_validation(port, release, timeout=180):
    """Poll the running app without ROM entry, DTR/RTS pulses or resets."""
    import serial
    deadline = time.monotonic() + timeout
    expected = f"USB_UPDATE_READY release={release} ready=1".encode()
    last_status = None
    while time.monotonic() < deadline:
        connection = validation_serial()
        connection.port = port
        connection.baudrate = 115200
        connection.timeout = 1
        try:
            connection.open()
            # Older Buddy releases do not implement 'v'. Their read-only '?'
            # report still identifies the running image after a rollback.
            connection.write(b"?")
            while time.monotonic() < deadline:
                connection.write(b"v")
                reply = connection.readline().strip()
                if reply.startswith((b"USB_UPDATE_READY release=", b"FIRMWARE BUDDY:")):
                    status = reply.decode("ascii", errors="replace")
                    if status != last_status:
                        print(f"Startup validation: {status}", flush=True)
                    last_status = status
                if reply == expected:
                    return
                time.sleep(0.25)
        except (serial.SerialException, OSError):
            time.sleep(1)  # USB may briefly disappear while the new app boots.
        finally:
            connection.close()
    detail = f" Last app response: {last_status}." if last_status else " No app validation response was received."
    raise UpdateError(
        "The new app did not confirm healthy startup. No read-back reset was sent; "
        "the update may be installed but is not verified. Keep Buddy powered and "
        "resolve the startup fault before restarting. Your backup remains available." + detail
    )


def write_and_verify(base, port, baud, address, data, label, selector_sequence=None,
                     validation_release=None):
    with tempfile.NamedTemporaryFile(prefix="buddy-" + label + "-", suffix=".bin", delete=False) as handle:
        path = Path(handle.name)
        handle.write(data)
    try:
        args = ["--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
                hex(address), str(path)]
        run_esptool(base, port, "write-flash", *args, baud=baud,
                    after="watchdog-reset" if selector_sequence is not None else "hard-reset")
        if selector_sequence is not None:
            # The selector is NEW, not pending: its initial full-system reset
            # must leave latched ROM download mode as well. Let first boot reach
            # esp_ota_mark_app_valid_cancel_rollback() before read-flash
            # reconnects and resets it again.
            print("Allowing Buddy to complete first-boot validation before checking the selector…",
                  flush=True)
            wait_for_boot_validation(port, validation_release)
        read_bauds = (baud, 115200, 115200)
        last_problem = "read-back did not match the written data"
        for attempt, read_baud in enumerate(read_bauds, start=1):
            with tempfile.NamedTemporaryFile(prefix="buddy-readback-", suffix=".bin", delete=False) as check:
                check_path = Path(check.name)
            try:
                run_esptool(base, port, "read-flash", hex(address), hex(len(data)), check_path,
                            baud=read_baud,
                            after="watchdog-reset" if selector_sequence is not None else "hard-reset")
                actual = check_path.read_bytes()
                selector_ok = (selector_sequence is not None and
                               selector_readback_matches(data, actual, selector_sequence))
                if selector_ok or (selector_sequence is None and
                                   hashlib.sha256(actual).digest() == hashlib.sha256(data).digest()):
                    return
                if selector_sequence is not None and len(actual) >= OTA_ENTRY.size:
                    _read_sequence, _label, state, _crc = OTA_ENTRY.unpack_from(actual)
                    last_problem = (f"OTA selector state is {state}; expected new/valid sequence "
                                    f"{selector_sequence} to be confirmed valid (state 2)")
                else:
                    last_problem = "read-back did not match the written data"
            except (UpdateError, OSError) as exc:
                last_problem = str(exc)
            finally:
                check_path.unlink(missing_ok=True)
            if attempt < len(read_bauds):
                print(f"Read-back check {attempt} was inconclusive; retrying at {read_bauds[attempt]} baud…",
                      flush=True)
        raise UpdateError(
            f"Could not verify {label} at 0x{address:x} after {len(read_bauds)} reads: {last_problem}. "
            "The write may have reached flash. Keep the full backup; do not use the first-install flasher."
        )
    finally:
        path.unlink(missing_ok=True)



def validate_partition_layout(flash):
    expected = (BUNDLED_IMAGE.parent / "partitions.bin").read_bytes()
    if len(flash) != FLASH_SIZE or flash[0x8000:0x8000 + len(expected)] != expected:
        raise UpdateError("Partition table differs from Buddy's supported layout; no firmware was written.")


def save_restore_receipt(path, flash, mac, current, active_slot, release):
    start = APP_OFFSETS[active_slot]
    record = {"board_id": mac, "previous_release": current, "previous_slot": active_slot,
              "installed_release": release,
              "previous_slot_sha256": hashlib.sha256(flash[start:start + APP_SIZE]).hexdigest()}
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.partial")
    temporary.write_text(json.dumps(record, indent=2) + "\n")
    temporary.replace(path)


def prepare_restore(flash, receipt_path, mac):
    try:
        receipt = json.loads(receipt_path.read_text())
        if receipt["board_id"] != mac:
            raise UpdateError("This recovery receipt belongs to another Buddy.")
        active, latest = active_ota_entry(flash)
        active_slot = (active["sequence"] - 1) % 2
        target_slot = receipt["previous_slot"]
        if target_slot not in (0, 1) or target_slot == active_slot:
            raise UpdateError("The previous app is already active or no longer available.")
        current = flash[APP_OFFSETS[active_slot]:APP_OFFSETS[active_slot] + APP_SIZE]
        marker = MARKER_PREFIX + str(receipt["installed_release"]).encode() + b"UL"
        if marker not in current:
            raise UpdateError("This board is no longer running the release recorded for restoration.")
        start = APP_OFFSETS[target_slot]
        prior = flash[start:start + APP_SIZE]
        if hashlib.sha256(prior).hexdigest() != receipt["previous_slot_sha256"]:
            raise UpdateError("The previous app slot changed; refusing to select an unverified image.")
        sequence = latest["sequence"] + 1
        if (sequence - 1) % 2 != target_slot:
            sequence += 1
        if sequence >= 0xFFFFFFFF:
            raise UpdateError("OTA sequence rollover requires manual recovery.")
        selector = bytearray(b"\xff" * OTA_SECTOR_SIZE)
        selector[:OTA_ENTRY.size] = make_ota_entry(sequence)
        return receipt["previous_release"], sequence, 1 - active["sector"], bytes(selector)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise UpdateError(f"A valid recovery receipt for this board is required in {receipt_path.parent}.") from exc

def main(argv=None):
    parser = argparse.ArgumentParser(description="Apply a later Buddy release to one existing Buddy over USB")
    parser.add_argument("--port", help="USB serial port; normally detected automatically")
    parser.add_argument("--verify-files", action="store_true", help="validate BUDDY.UPD without connecting to a board")
    parser.add_argument("--restore-previous-app", action="store_true",
                        help="return this board to its saved previous app slot, keeping its current life")
    args = parser.parse_args(argv)
    release, image = parse_package(PACKAGE)
    print(f"Buddy {release} — USB firmware update", flush=True)
    print("Target: Waveshare ESP32-S3-ePaper-1.54 V2 monochrome (non-touch)", flush=True)
    if args.verify_files:
        print(f"Package valid: {len(image)} byte app image. No board was touched.")
        return 0
    base = esptool_command()
    port = args.port or discover_port()
    print(f"Found USB port: {port}", flush=True)
    hardware = run_esptool(base, port, "flash-id", capture=True)
    mac = validate_hardware(hardware)
    print(f"Verified ESP32-S3-PICO-1, 8 MB flash/PSRAM, ID {mac}.", flush=True)
    security = run_esptool(base, port, "get-security-info", capture=True)
    if "Secure Boot: Disabled" not in security or "Flash Encryption: Disabled" not in security:
        raise UpdateError("Flash security is enabled or unknown; refusing this update method.")
    backup_path, baud = read_full_backup(base, port, mac)
    flash = backup_path.read_bytes()
    validate_partition_layout(flash)
    receipt_path = BACKUPS / (mac + "-previous.json")
    if args.restore_previous_app:
        receipt_path = restore_receipt(ROOT, BACKUPS, mac)
        previous_release, sequence, selector_sector, selector = prepare_restore(flash, receipt_path, mac)
        write_and_verify(base, port, baud, OTA_DATA + selector_sector * OTA_SECTOR_SIZE,
                         selector, "ota-selector", selector_sequence=sequence,
                         validation_release=previous_release)
        wait_for_boot_validation(port, previous_release)
        print(f"Previous firmware {previous_release} is running. Current life and world data were preserved.")
        return 0
    current, active_slot, target_slot, sequence, selector_sector, selector = prepare_update(flash, release)
    save_restore_receipt(receipt_path, flash, mac, current, active_slot, release)
    print(f"Installed Buddy: {current}; update: {release}.")
    print(f"Writing inactive app slot {target_slot} at 0x{APP_OFFSETS[target_slot]:x};")
    print(f"the existing slot {active_slot}, life data, partitions, model and current boot selector are preserved.")
    write_and_verify(base, port, baud, APP_OFFSETS[target_slot], image, "app")
    selector_address = OTA_DATA + selector_sector * OTA_SECTOR_SIZE
    write_and_verify(base, port, baud, selector_address, selector, "ota-selector",
                     selector_sequence=sequence, validation_release=release)
    # Native USB RTS is a core reset and can leave the chip in ROM download
    # mode. Selector readback above uses a full-system watchdog reset, but
    # never declare success solely from flash contents: prove the app resumed.
    print("Checking Buddy is running normally after the final restart…", flush=True)
    wait_for_boot_validation(port, release)
    print(f"Update {release} was written, read-back verified, and confirmed running after restart.")
    print(f"A full recovery backup is saved at {backup_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (UpdateError, OSError, KeyboardInterrupt, EOFError) as exc:
        print(f"\nStopped safely: {exc}", file=sys.stderr)
        raise SystemExit(1)
