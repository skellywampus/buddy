import importlib.util
import hashlib
from pathlib import Path
import re
import struct
import tempfile
import unittest
import sys
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[1] / "flash_new_buddy.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("flash_new_buddy", MODULE_PATH)
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def test_bundle_matches_release_and_offsets(self):
        manifest, paths = installer.verify_bundle()
        source = installer.SOURCE_VERSION.read_text()
        expected = int(re.search(r"BUDDY_FIRMWARE_VERSION (\d+)UL", source).group(1))
        self.assertEqual(manifest["release"], expected)
        self.assertEqual(len(paths), 5)
        self.assertEqual(tuple(int(x["address"], 0) for x in manifest["images"]),
                         installer.EXPECTED_ADDRESSES)
        self.assertEqual(tuple(x["max_size"] for x in manifest["images"]),
                         installer.EXPECTED_MAX_SIZES)

    def test_rejects_stale_firmware_source(self):
        with tempfile.TemporaryDirectory() as directory:
            stale = Path(directory) / "buddy_update_format.h"
            stale.write_text("#define BUDDY_FIRMWARE_VERSION 2026100701UL\n")
            with patch.object(installer, "SOURCE_VERSION", stale):
                with self.assertRaisesRegex(installer.InstallError, "do not match"):
                    installer.verify_bundle()

    def test_sd_package_matches_first_install_app_when_present(self):
        update = installer.ROOT / "BUDDY.UPD"
        if not update.is_file():
            self.skipTest("The ignored local SD update package is not present")
        manifest, paths = installer.verify_bundle()
        data = update.read_bytes()
        self.assertGreaterEqual(len(data), 68)
        magic, format_, target, release, size, digest = struct.unpack("<8sI16sII32s", data[:68])
        image = paths[3].read_bytes()
        self.assertEqual((magic, format_, target.rstrip(b"\0"), release),
                         (b"BUDDYUP1", 1, b"WS154V2-BW", manifest["release"]))
        self.assertEqual(size, len(image))
        self.assertEqual(data[68:], image)
        self.assertEqual(digest, hashlib.sha256(image).digest())

    def test_reject_existing_buddy_even_if_no_life_yet(self):
        flash = bytearray(b"\xff" * 0x800000)
        self.assertFalse(installer.already_buddy(flash))
        flash[0x14000:0x14000 + len(installer.BUDDY_MARKER)] = installer.BUDDY_MARKER
        self.assertTrue(installer.already_buddy(flash))
        flash[0x14000:0x14000 + len(installer.BUDDY_MARKER)] = b"\xff" * len(installer.BUDDY_MARKER)
        flash[0x9100:0x910b] = b"buddy-life\x00"
        self.assertTrue(installer.already_buddy(flash))

    def test_hardware_gate(self):
        output = ("Chip type: ESP32-S3-PICO-1 (LGA56)\n"
                  "Features: Wi-Fi, Embedded Flash 8MB (GD), Embedded PSRAM 8MB\n"
                  "Detected flash size: 8MB\nMAC: 00:11:22:33:44:55\n")
        self.assertEqual(installer.validate_hardware(output), "001122334455")
        for changed in (output.replace("PICO-1", "WROOM-1"),
                        output.replace("Flash 8MB", "Flash 4MB"),
                        output.replace("PSRAM 8MB", "PSRAM 2MB"),
                        output.replace("size: 8MB", "size: 4MB")):
            with self.assertRaises(installer.InstallError):
                installer.validate_hardware(changed)
        installer.validate_security("Secure Boot: Disabled\nFlash Encryption: Disabled\n")
        for report in ("Secure Boot: Enabled\nFlash Encryption: Disabled\n",
                       "Secure Boot: Disabled\nFlash Encryption: Enabled\n", ""):
            with self.assertRaises(installer.InstallError):
                installer.validate_security(report)

    def test_port_detection_refuses_ambiguity(self):
        class Port:
            def __init__(self, device, vid=0x303A):
                self.device, self.vid, self.description = device, vid, "Espressif USB"

        with patch("serial.tools.list_ports.comports", return_value=[Port("/dev/cu.usbmodem1101")]):
            self.assertEqual(installer.discover_port(), "/dev/cu.usbmodem1101")
        with patch("serial.tools.list_ports.comports", return_value=[]):
            with self.assertRaises(installer.InstallError):
                installer.discover_port(timeout=0)
        with patch("serial.tools.list_ports.comports",
                   return_value=[Port("/dev/cu.usbmodem1101"), Port("/dev/cu.usbmodem1102")]):
            with self.assertRaises(installer.InstallError):
                installer.discover_port()

    def test_backup_rejects_an_existing_life(self):
        with tempfile.TemporaryDirectory() as directory:
            def fake_read(_base, _port, operation, _start, _size, destination, **_kwargs):
                self.assertEqual(operation, "read-flash")
                flash = bytearray(b"\xff" * 0x800000)
                flash[0x9100:0x910b] = b"buddy-life\x00"
                Path(destination).write_bytes(flash)

            with patch.object(installer, "BACKUPS", Path(directory)), \
                 patch.object(installer, "run_esptool", side_effect=fake_read):
                with self.assertRaisesRegex(installer.InstallError, "existing life"):
                    installer.backup_flash(["esptool"], "/dev/cu.usbmodem1", "001122334455")

    def test_backup_retries_at_safe_baud_and_only_promotes_complete_file(self):
        with tempfile.TemporaryDirectory() as directory:
            rates = []

            def fake_read(_base, _port, operation, _start, _size, destination, **kwargs):
                self.assertEqual(operation, "read-flash")
                rate = kwargs["baud"]
                rates.append(rate)
                if rate == 460800:
                    Path(destination).write_bytes(b"partial")
                    raise installer.InstallError("serial stream stopped")
                Path(destination).write_bytes(b"\xff" * 0x800000)

            with patch.object(installer, "BACKUPS", Path(directory)), \
                 patch.object(installer, "run_esptool", side_effect=fake_read):
                path, rate = installer.backup_flash(["esptool"], "/dev/cu.usbmodem1",
                                                     "001122334455")
            self.assertEqual(rates, [460800, 115200])
            self.assertEqual(rate, 115200)
            self.assertEqual(path.stat().st_size, 0x800000)
            self.assertTrue(list(Path(directory).glob("*.partial")))

    def test_backup_failure_never_creates_a_restorable_file(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(installer, "BACKUPS", Path(directory)), \
                 patch.object(installer, "run_esptool",
                              side_effect=installer.InstallError("serial stream stopped")):
                with self.assertRaisesRegex(installer.InstallError, "no firmware was written"):
                    installer.backup_flash(["esptool"], "/dev/cu.usbmodem1",
                                           "001122334455")
            self.assertFalse(list(Path(directory).glob("*.bin")))

    def test_install_writes_only_five_expected_ranges_after_confirmation(self):
        output = ("Chip type: ESP32-S3-PICO-1\n"
                  "Features: Embedded Flash 8MB, Embedded PSRAM 8MB\n"
                  "Detected flash size: 8MB\nMAC: 00:11:22:33:44:55\n")
        calls = []

        def fake_tool(_base, _port, operation, *arguments, **_kwargs):
            calls.append((operation, arguments, _kwargs))
            return (output if operation == "flash-id" else
                    "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
                    if operation == "get-security-info" else "")

        with patch.object(installer, "esptool_command", return_value=["esptool"]), \
             patch.object(installer, "discover_port", return_value="/dev/cu.usbmodem1"), \
             patch.object(installer, "backup_flash", return_value=("backup.bin", 115200)), \
             patch.object(installer, "verify_written_images") as verify, \
             patch.object(installer, "wait_for_first_boot") as boot, \
             patch.object(installer, "run_esptool", side_effect=fake_tool), \
             patch("builtins.input", return_value="V2"):
            self.assertEqual(installer.main([]), 0)
        self.assertEqual([name for name, _, _ in calls],
                         ["flash-id", "get-security-info", "flash-id", "get-security-info", "write-flash", "read-mac"])
        arguments = calls[-2][1]
        self.assertEqual(arguments[:6], ("--flash-mode", "dio", "--flash-freq", "80m",
                                         "--flash-size", "8MB"))
        self.assertEqual(tuple(arguments[6::2]),
                         tuple(hex(address) for address in installer.EXPECTED_ADDRESSES))
        self.assertEqual(calls[-2][2]["baud"], 115200)
        self.assertEqual(calls[-1][2]["after"], "watchdog-reset")
        verify.assert_called_once()
        boot.assert_called_once()

    def test_cancel_never_writes(self):
        calls = []

        def fake_tool(_base, _port, operation, *arguments, **_kwargs):
            calls.append(operation)
            if operation == "flash-id":
                return ("Chip type: ESP32-S3-PICO-1\nFeatures: Embedded Flash 8MB, "
                        "Embedded PSRAM 8MB\nMAC: 00:11:22:33:44:55\n")
            return "Secure Boot: Disabled\nFlash Encryption: Disabled\n"

        with patch.object(installer, "esptool_command", return_value=["esptool"]), \
             patch.object(installer, "discover_port", return_value="/dev/cu.usbmodem1"), \
             patch.object(installer, "backup_flash", return_value=("backup.bin", 460800)), \
             patch.object(installer, "run_esptool", side_effect=fake_tool), \
             patch("builtins.input", return_value="no"):
            self.assertEqual(installer.main([]), 1)
        self.assertEqual(calls, ["flash-id", "get-security-info"])


if __name__ == "__main__":
    unittest.main()
