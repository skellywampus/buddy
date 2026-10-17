"""No hardware is opened: transport, files and time are controlled by the tests."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


flash = load("flash_new_buddy")
launch = load("launch_first_flash")
diagnostics = load("usb_diagnostics")


class SafetyTests(unittest.TestCase):
    def test_failures_before_write_never_install(self):
        output = ("Chip type: ESP32-S3-PICO-1\nFeatures: Embedded Flash 8MB, Embedded PSRAM 8MB\n"
                  "MAC: 00:11:22:33:44:55\n")
        secure = "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
        for cause in ("backup", "identity", "security", "write", "readback"):
            events = []
            hardware_reads = 0
            def tool(_base, _port, operation, *args, **kwargs):
                nonlocal hardware_reads
                events.append(operation)
                if operation == "flash-id":
                    hardware_reads += 1
                    return output.replace("44:55", "44:66") if cause == "identity" and hardware_reads == 2 else output
                if operation == "get-security-info":
                    return secure.replace("Secure Boot: Disabled", "Secure Boot: Enabled") if cause == "security" else secure
                if operation == "write-flash" and cause == "write":
                    raise flash.InstallError("write failed")
                return ""
            with patch.object(flash, "esptool_command", return_value=[]), \
                 patch.object(flash, "discover_port", return_value="COM3"), \
                 patch.object(flash, "run_esptool", side_effect=tool), \
                 patch.object(flash, "backup_flash", side_effect=flash.InstallError("backup failed") if cause == "backup" else None,
                              return_value=("backup.bin", 115200)), \
                 patch.object(flash, "verify_written_images", side_effect=flash.InstallError("readback failed") if cause == "readback" else None), \
                 patch.object(flash, "wait_for_first_boot") as boot, patch("builtins.input", return_value="V2"):
                with self.assertRaises(flash.InstallError):
                    flash.main([])
                self.assertEqual(events.count("write-flash"), int(cause in ("write", "readback")))
                self.assertNotIn("read-mac", events)
                boot.assert_not_called()

    def test_cross_platform_discovery_and_duplicate_ports(self):
        for name in ("COM3", "COM125", "/dev/cu.usbmodem1", "/dev/ttyACM0", "/dev/ttyUSB0"):
            port = SimpleNamespace(device=name, vid=0x303A)
            with patch("serial.tools.list_ports.comports", return_value=[port, port]):
                self.assertEqual(flash.discover_port(timeout=0), name)
        for name, vid in (("COM4", 0x1234), ("/dev/tty.Bluetooth", 0x303A)):
            with patch("serial.tools.list_ports.comports", return_value=[SimpleNamespace(device=name, vid=vid)]):
                with self.assertRaises(flash.InstallError):
                    flash.discover_port(timeout=0)

    def test_discovery_waits_then_finds_without_opening(self):
        port = SimpleNamespace(device="COM5", vid=0x303A)
        with patch("serial.tools.list_ports.comports", side_effect=[[], [port]]), \
             patch.object(flash.time, "sleep") as sleep:
            self.assertEqual(flash.discover_port(), "COM5")
            sleep.assert_called_once_with(1)

    def test_manifest_rejects_invalid_structure_and_paths(self):
        valid = json.loads(flash.MANIFEST.read_text())
        cases = [[], {**valid, "images": []}]
        for field, value in (("file", "../Buddy.ino.bin"), ("address", "not-hex"), ("max_size", 0x800000)):
            changed = json.loads(json.dumps(valid))
            changed["images"][3][field] = value
            cases.append(changed)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            for data in cases:
                path.write_text(json.dumps(data))
                with patch.object(flash, "MANIFEST", path):
                    with self.assertRaises(flash.InstallError):
                        flash.verify_bundle()

    def test_missing_image_fails_before_any_usb_discovery(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(flash, "IMAGES", Path(directory)), \
             patch.object(flash, "discover_port") as discover:
            with self.assertRaises(flash.InstallError):
                flash.main([])
            discover.assert_not_called()

    def test_file_validation_is_offline(self):
        with patch.object(flash, "esptool_command") as tool, patch.object(flash, "discover_port") as ports:
            self.assertEqual(flash.main(["--verify-files"]), 0)
            tool.assert_not_called()
            ports.assert_not_called()

    def test_rom_operations_default_to_no_reset(self):
        with patch.object(flash.subprocess, "run", return_value=Mock(returncode=0, stdout="")) as run:
            flash.run_esptool(["tool"], "COM3", "write-flash", "0x0", "boot.bin")
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--after") + 1], "no-reset")
        self.assertNotIn("erase-flash", command)

    def test_readback_checks_all_five_offsets(self):
        manifest, paths = flash.verify_bundle()
        by_offset = {item["address"]: path.read_bytes() for item, path in zip(manifest["images"], paths)}
        calls = []
        def read(_base, _port, operation, offset, length, target, **kwargs):
            self.assertEqual(operation, "read-flash")
            calls.append(offset)
            self.assertEqual(int(length, 0), len(by_offset[offset]))
            Path(target).write_bytes(by_offset[offset])
        with patch.object(flash, "run_esptool", side_effect=read):
            flash.verify_written_images([], "COM3", 460800, manifest, paths)
        self.assertEqual(calls, list(by_offset))

    def test_readback_retries_and_stops_at_persistent_mismatch(self):
        manifest, paths = flash.verify_bundle()
        rates = []
        def bad_read(_base, _port, operation, offset, length, target, **kwargs):
            rates.append(kwargs["baud"])
            Path(target).write_bytes(b"corrupt")
        with patch.object(flash, "run_esptool", side_effect=bad_read):
            with self.assertRaisesRegex(flash.InstallError, "Read-back verification failed"):
                flash.verify_written_images([], "COM3", 460800, manifest, paths)
        self.assertEqual(rates, [460800, 115200])

    def test_transient_readback_error_can_recover(self):
        manifest, paths = flash.verify_bundle()
        manifest = {"images": manifest["images"][:1]}
        def read(_base, _port, operation, offset, length, target, **kwargs):
            if kwargs["baud"] == 460800:
                raise flash.InstallError("interrupted read")
            Path(target).write_bytes(paths[0].read_bytes())
        with patch.object(flash, "run_esptool", side_effect=read):
            flash.verify_written_images([], "COM3", 460800, manifest, paths[:1])

    def test_no_space_means_no_backup_or_flash_operation(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(flash, "BACKUPS", Path(directory)), \
             patch.object(flash.shutil, "disk_usage", return_value=SimpleNamespace(free=0)), \
             patch.object(flash, "run_esptool") as tool:
            with self.assertRaisesRegex(flash.InstallError, "free disk space"):
                flash.backup_flash([], "COM3", "1234")
            tool.assert_not_called()

    @staticmethod
    def report():
        report = {name: f"{name} save_ok=1 recovery=0" for name in ("BUDDY", "HOME", "AMBITION", "LEGACY", "CABLE", "INTRO")}
        report["FIRMWARE"] = "FIRMWARE BUDDY:WS154V2-BW:2026101305UL"
        report["BUDDY"] += " clock_ok=1 alive=0 gen=0"
        report["INTRO"] += " required=1"
        report["POWER_SERVICE"] = "POWER_SERVICE battery_latch=1 wake_ready=1"
        report["RECOVERY"] = "RECOVERY wdt=1"
        return report

    def test_first_boot_welcome_is_valid_without_ota_valid_ack(self):
        self.assertTrue(flash.first_boot_report_ready(self.report(), 2026101305))
        report = self.report()
        report["INTRO"] = report["INTRO"].replace("required=1", "required=0")
        self.assertFalse(flash.first_boot_report_ready(report, 2026101305))
        report["BUDDY"] = report["BUDDY"].replace("alive=0 gen=0", "alive=1 gen=1")
        self.assertTrue(flash.first_boot_report_ready(report, 2026101305))

    def test_startup_rejects_wrong_release_or_unhealthy_subsystems(self):
        self.assertFalse(flash.first_boot_report_ready(self.report(), 1234))
        for name, field in (("BUDDY", "clock_ok"), ("HOME", "save_ok"), ("INTRO", "save_ok"),
                            ("POWER_SERVICE", "battery_latch"), ("POWER_SERVICE", "wake_ready"), ("RECOVERY", "wdt")):
            report = self.report()
            report[name] = report[name].replace(field + "=1", field + "=0")
            self.assertFalse(flash.first_boot_report_ready(report, 2026101305))

    def test_status_transport_closes_and_only_queries(self):
        report = self.report()
        connection = Mock()
        connection.readline.side_effect = [(line + "\n").encode() for line in report.values()]
        with patch.dict(sys.modules, {"usb_diagnostics": diagnostics}), \
             patch.object(diagnostics, "open_diagnostic", return_value=connection):
            flash.wait_for_first_boot("COM3", 2026101305)
        connection.write.assert_called_once_with(b"?")
        connection.close.assert_called_once()

    def test_startup_timeout_never_resets_or_writes(self):
        with patch.dict(sys.modules, {"usb_diagnostics": diagnostics}), \
             patch.object(diagnostics, "open_diagnostic") as open_, \
             patch.object(flash, "run_esptool") as tool:
            with self.assertRaisesRegex(flash.InstallError, "normal startup was not confirmed"):
                flash.wait_for_first_boot("COM3", 2026101305, timeout=0)
            open_.assert_not_called()
            tool.assert_not_called()


class BootstrapTests(unittest.TestCase):
    def test_verify_and_help_skip_environment_and_downloads(self):
        for args in (["--verify-files"], ["--help"]):
            with patch.object(launch.subprocess, "run", return_value=Mock(returncode=0)) as run, \
                 patch.object(launch, "cache_root") as cache:
                self.assertEqual(launch.main(args), 0)
                cache.assert_not_called()
                self.assertEqual(run.call_count, 1)

    def test_bad_bundle_stops_before_environment_setup(self):
        with patch.object(launch.subprocess, "run", return_value=Mock(returncode=2)), \
             patch.object(launch, "cache_root") as cache:
            self.assertEqual(launch.main([]), 2)
            cache.assert_not_called()

    def test_cached_environment_needs_no_install(self):
        with patch.object(launch, "cache_root", return_value=Path("cached")), \
             patch.object(launch, "dependencies_ready", return_value=True), \
             patch.object(launch.venv, "EnvBuilder") as builder, \
             patch.object(launch.subprocess, "run", return_value=Mock(returncode=0)) as run:
            self.assertEqual(launch.main(["--port", "COM3"]), 0)
            builder.assert_not_called()
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args.args[0][-2:], ["--port", "COM3"])

    def test_new_environment_installs_only_pinned_dependencies(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(launch, "cache_root", return_value=Path(directory)), \
             patch.object(launch, "dependencies_ready", side_effect=[False, True]), \
             patch.object(launch.venv, "EnvBuilder") as builder, \
             patch.object(launch.subprocess, "run", return_value=Mock(returncode=0)) as run:
            self.assertEqual(launch.main([]), 0)
            builder.assert_called_once_with(with_pip=True)
            install = run.call_args_list[1].args[0]
            self.assertEqual(install[-2:], ["esptool==5.3.0", "pyserial==3.5"])
            self.assertNotIn("--user", install)

    def test_platform_cache_locations_and_python_paths(self):
        cases = [("nt", "win32", {"LOCALAPPDATA": "/tmp/local"}, "/tmp/local/Buddy/first-flash", "Scripts/python.exe"),
                 ("posix", "darwin", {}, str(Path.home() / "Library/Caches/Buddy/first-flash"), "bin/python"),
                 ("posix", "linux", {"XDG_CACHE_HOME": "/tmp/cache"}, "/tmp/cache/Buddy/first-flash", "bin/python")]
        for name, platform, env, prefix, suffix in cases:
            with patch.object(launch, "os", SimpleNamespace(name=name, environ=env)), \
                 patch.object(launch, "sys", SimpleNamespace(platform=platform, version_info=sys.version_info)):
                self.assertTrue(str(launch.cache_root()).startswith(str(Path(prefix))))
                self.assertEqual(launch.environment_python(Path("cache")), Path("cache") / suffix)


class DiagnosticTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "native Windows API smoke test")
    def test_windows_invalid_port_fails_cleanly(self):
        with self.assertRaises(OSError):
            diagnostics.WindowsDiagnostic("COM9999")

    def test_windows_transport_configures_preserving_lines_and_closes_on_failure(self):
        import ctypes
        class Dcb(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint32) for name in (
                "DCBlength", "BaudRate", "ByteSize", "Parity", "StopBits", "fBinary", "fParity",
                "fOutxCtsFlow", "fOutxDsrFlow", "fOutX", "fInX", "fNull", "fErrorChar",
                "fAbortOnError", "fDtrControl", "fRtsControl")]
        class Timeouts(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint32) for name in ("ReadIntervalTimeout", "ReadTotalTimeoutConstant", "WriteTotalTimeoutConstant")]
        for fails in (False, True):
            def get_state(handle, state):
                state._obj.fDtrControl, state._obj.fRtsControl = 1, 2
                return 1
            def set_state(handle, state):
                self.assertEqual((state._obj.fDtrControl, state._obj.fRtsControl), (1, 2))
                return not fails
            def write(handle, data, length, count, overlapped):
                count._obj.value = length
                return 1
            pending = iter(b"OK\n")
            def read(handle, buffer, length, count, overlapped):
                buffer.raw = bytes([next(pending)])
                count._obj.value = 1
                return 1
            api = SimpleNamespace(CreateFile=Mock(return_value=123), GENERIC_READ=1, GENERIC_WRITE=2,
                OPEN_EXISTING=3, INVALID_HANDLE_VALUE=-1, DCB=Dcb, COMMTIMEOUTS=Timeouts, MAXDWORD=0xffffffff,
                DWORD=ctypes.c_uint32, GetCommState=get_state, SetCommState=set_state,
                SetCommTimeouts=Mock(return_value=1), CloseHandle=Mock(), WriteFile=write, ReadFile=read)
            import serial
            with patch.object(serial, "win32", api, create=True):
                if fails:
                    with self.assertRaises(OSError):
                        diagnostics.WindowsDiagnostic("COM3")
                else:
                    connection = diagnostics.WindowsDiagnostic("COM3")
                    connection.write(b"?")
                    self.assertEqual(connection.readline(), b"OK\n")
                    connection.close()
                    connection.close()
            self.assertEqual(api.CreateFile.call_args.args[0], "\\\\.\\COM3")
            api.CloseHandle.assert_called_once_with(123)

    def test_windows_configuration_preserves_dtr_and_rts(self):
        for dtr in range(3):
            for rts in range(4):
                dcb = SimpleNamespace(fDtrControl=dtr, fRtsControl=rts)
                diagnostics.configure_windows_dcb(dcb)
                self.assertEqual((dcb.fDtrControl, dcb.fRtsControl), (dtr, rts))
                self.assertEqual((dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits), (115200, 8, 0, 0))
                self.assertEqual((dcb.fOutxCtsFlow, dcb.fOutxDsrFlow, dcb.fInX, dcb.fOutX), (0, 0, 0, 0))

    @unittest.skipIf(sys.platform == "win32", "POSIX pseudo-terminal test")
    def test_posix_diagnostics_clear_hupcl_without_dtr_rts_updates(self):
        import os
        import pty
        import serial
        import termios
        master, slave = pty.openpty()
        connection = None
        try:
            with patch.object(serial.Serial, "_update_dtr_state") as dtr, \
                 patch.object(serial.Serial, "_update_rts_state") as rts:
                connection = diagnostics.open_diagnostic(os.ttyname(slave))
                self.assertFalse(termios.tcgetattr(connection.fileno())[2] & termios.HUPCL)
                connection.write(b"?")
                self.assertEqual(os.read(master, 1), b"?")
                dtr.assert_not_called()
                rts.assert_not_called()
        finally:
            if connection:
                connection.close()
            os.close(master)
            os.close(slave)


if __name__ == "__main__":
    unittest.main()
