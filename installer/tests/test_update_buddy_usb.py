import hashlib
import os
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest
import sys
from unittest.mock import patch, Mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "update_buddy_usb.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("update_buddy_usb", MODULE_PATH)
updater = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(updater)


class UsbUpdateTests(unittest.TestCase):
    def test_esptool_full_system_reset_option_is_explicit(self):
        with patch.object(updater.subprocess,"run",return_value=Mock(returncode=0,stdout="ok")) as run:
            updater.run_esptool(["esptool"],"/dev/mock","read-flash","0xf000","0x1000","output.bin",
                                after="watchdog-reset")
        command=run.call_args.args[0]
        self.assertEqual(command[command.index("--after")+1],"watchdog-reset")

    def test_confirmed_selector_readback_uses_full_system_reset_only_after_acceptance(self):
        selector=bytearray(b"\xff"*updater.OTA_SECTOR_SIZE)
        selector[:updater.OTA_ENTRY.size]=updater.make_ota_entry(8)
        confirmed=bytearray(selector)
        struct.pack_into("<I",confirmed,24,2)
        events=[]
        def fake_run(_base,_port,operation,*args,**kwargs):
            events.append((operation,kwargs.get("after","hard-reset")))
            if operation=="read-flash":Path(args[-1]).write_bytes(confirmed)
        with patch.object(updater,"run_esptool",side_effect=fake_run), \
             patch.object(updater,"wait_for_boot_validation",side_effect=lambda *_:events.append(("healthy",None))):
            updater.write_and_verify(["esptool"],"/dev/mock",115200,0xf000,bytes(selector),"ota-selector",
                                     selector_sequence=8,validation_release=2026101304)
        self.assertEqual(events,[("write-flash","watchdog-reset"),("healthy",None),("read-flash","watchdog-reset")])

    def test_detection_waits_for_a_sleeping_buddy_to_appear(self):
        port=Mock(device="/dev/cu.usbmodem1",vid=0x303a)
        with patch("serial.tools.list_ports.comports",side_effect=[[],[],[port]]), \
             patch.object(updater.time,"monotonic",return_value=0), \
             patch.object(updater.time,"sleep") as sleep:
            self.assertEqual(updater.discover_port(),port.device)
            self.assertEqual(sleep.call_count,2)

    def test_detection_wait_is_bounded_and_non_mutating(self):
        with patch("serial.tools.list_ports.comports",return_value=[]), \
             patch.object(updater.time,"monotonic",side_effect=[0,0,75]), \
             patch.object(updater.time,"sleep"),patch.object(updater,"run_esptool") as esptool:
            with self.assertRaisesRegex(updater.UpdateError,"press BOOT"):
                updater.discover_port()
            esptool.assert_not_called()

    def test_waits_for_matching_healthy_app_without_reset_lines(self):
        connection = Mock()
        connection.readline.side_effect = [b"boot text\n",
            b"USB_UPDATE_READY release=2026101301 ready=1\n",
            b"USB_UPDATE_READY release=2026101302 ready=0\n",
            b"USB_UPDATE_READY release=2026101302 ready=1\n"]
        with patch.object(updater,"validation_serial", return_value=connection), patch.object(updater.time,"sleep"):
            updater.wait_for_boot_validation("/dev/mock",2026101302)
        self.assertNotIn("dtr",connection.__dict__)
        self.assertNotIn("rts",connection.__dict__)
        self.assertEqual(connection.write.call_count,5)
        self.assertEqual(connection.write.call_args_list[0].args,(b"?",))
        connection.close.assert_called_once()

    def test_validation_timeout_does_not_reset_for_readback(self):
        with patch.object(updater,"run_esptool") as esptool, \
             patch.object(updater,"wait_for_boot_validation",side_effect=updater.UpdateError("not ready")):
            with self.assertRaisesRegex(updater.UpdateError,"not ready"):
                updater.write_and_verify(["esptool"],"/dev/mock",115200,0xf000,
                    b"selector","ota-selector",selector_sequence=8,validation_release=2026101302)
        self.assertEqual(esptool.call_count,1)
        self.assertEqual(esptool.call_args.args[2],"write-flash")

    def test_startup_recovery_after_sixty_seconds_can_finish_before_readback(self):
        clock=[0.0]
        connection=Mock()
        def reply():
            clock[0]+=1
            return (b"USB_UPDATE_READY release=2026101302 ready=1\n" if clock[0]>=62 else
                    b"USB_UPDATE_READY release=2026101302 ready=0\n")
        connection.readline.side_effect=reply
        with patch.object(updater,"validation_serial",return_value=connection), \
             patch.object(updater.time,"monotonic",side_effect=lambda:clock[0]), \
             patch.object(updater.time,"sleep",side_effect=lambda seconds:clock.__setitem__(0,clock[0]+seconds)):
            updater.wait_for_boot_validation("/dev/mock",2026101302)
        self.assertGreaterEqual(clock[0],62)
        self.assertLess(clock[0],180)
        self.assertNotIn("dtr",connection.__dict__)
        self.assertNotIn("rts",connection.__dict__)

    def test_validation_retries_a_late_reenumeration_and_times_out_safely(self):
        import serial
        connection=Mock()
        connection.open.side_effect=serial.SerialException("not enumerated")
        with patch.object(updater,"validation_serial",return_value=connection), \
             patch.object(updater.time,"monotonic",side_effect=[0,0,1,181]), \
             patch.object(updater.time,"sleep"):
            with self.assertRaisesRegex(updater.UpdateError,"No read-back reset"):
                updater.wait_for_boot_validation("/dev/mock",2026101302)
        self.assertEqual(connection.open.call_count,2)

    @unittest.skipIf(os.name == "nt", "POSIX serial behavior")
    def test_real_posix_open_and_close_never_write_reset_lines(self):
        import os
        import pty
        import serial
        import termios
        master, slave = pty.openpty()
        connection = updater.validation_serial()
        try:
            attrs = termios.tcgetattr(slave)
            attrs[2] |= termios.HUPCL
            termios.tcsetattr(slave,termios.TCSANOW,attrs)
            connection.port = os.ttyname(slave)
            with patch.object(serial.Serial,"_update_dtr_state") as dtr, \
                 patch.object(serial.Serial,"_update_rts_state") as rts:
                for _ in range(2):
                    connection.open()
                    self.assertFalse(termios.tcgetattr(connection.fd)[2] & termios.HUPCL)
                    connection.write(b"v")
                    self.assertEqual(os.read(master,1),b"v")
                    connection.close()
                dtr.assert_not_called()
                rts.assert_not_called()
        finally:
            connection.close()
            os.close(master)
            os.close(slave)

    def test_timeout_includes_old_release_response(self):
        connection=Mock()
        connection.readline.return_value=b"USB_UPDATE_READY release=2026101301 ready=1\n"
        clock=[0]
        with patch.object(updater,"validation_serial",return_value=connection), \
             patch.object(updater.time,"monotonic",side_effect=lambda:clock[0]), \
             patch.object(updater.time,"sleep",side_effect=lambda seconds:clock.__setitem__(0,clock[0]+1)):
            with self.assertRaisesRegex(updater.UpdateError,"Last app response: USB_UPDATE_READY release=2026101301 ready=1"):
                updater.wait_for_boot_validation("/dev/mock",2026101304,timeout=2)
        connection.close.assert_called_once()

    def test_old_firmware_status_is_diagnostic_not_acceptance(self):
        connection=Mock()
        connection.readline.return_value=b"FIRMWARE BUDDY:WS154V2-BW:2026101301UL\n"
        clock=[0]
        with patch.object(updater,"validation_serial",return_value=connection), \
             patch.object(updater.time,"monotonic",side_effect=lambda:clock[0]), \
             patch.object(updater.time,"sleep",side_effect=lambda seconds:clock.__setitem__(0,clock[0]+1)):
            with self.assertRaisesRegex(updater.UpdateError,"Last app response: FIRMWARE BUDDY:WS154V2-BW:2026101301UL"):
                updater.wait_for_boot_validation("/dev/mock",2026101304,timeout=2)
        connection.close.assert_called_once()

    def test_current_package_matches_source_and_bundled_image(self):
        release, image = updater.parse_package(updater.PACKAGE)
        source = updater.SOURCE_VERSION.read_text()
        expected = int(re.search(r"BUDDY_FIRMWARE_VERSION (\d+)UL", source).group(1))
        self.assertEqual(release, expected)
        self.assertEqual(image, updater.BUNDLED_IMAGE.read_bytes())

    def test_refuses_self_consistent_but_stale_package(self):
        release = 2026100805
        image = b"\xe9" + bytes(11) + b"\x09\x00" + bytes(18) + f"BUDDY:WS154V2-BW:{release}UL".encode()
        package = updater.HEADER.pack(b"BUDDYUP1", 1, updater.TARGET, release,
                                      len(image), hashlib.sha256(image).digest()) + image
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "BUDDY.UPD"
            path.write_bytes(package)
            with self.assertRaisesRegex(updater.UpdateError, "does not match the current firmware bundle"):
                updater.parse_package(path)

    def test_refuses_multiple_detected_buddies(self):
        class Port:
            def __init__(self, device):
                self.device, self.vid = device, 0x303A

        with patch("serial.tools.list_ports.comports",
                   return_value=[Port("/dev/cu.usbmodem1"), Port("/dev/cu.usbmodem2")]):
            with self.assertRaisesRegex(updater.UpdateError, "More than one ESP32 USB port"):
                updater.discover_port()

    def test_chooses_inactive_slot_and_other_selector_sector(self):
        flash = bytearray(b"\xff" * updater.FLASH_SIZE)
        image = b"\xe9BUDDY:WS154V2-BW:2026100702UL"
        flash[updater.APP_OFFSETS[0]:updater.APP_OFFSETS[0] + len(image)] = image
        flash[updater.OTA_DATA:updater.OTA_DATA + updater.OTA_ENTRY.size] = \
            updater.make_ota_entry(1)

        result = updater.prepare_update(flash, 2026100806)
        current, active_slot, target_slot, sequence, selector_sector, selector = result
        self.assertEqual((current, active_slot, target_slot, sequence, selector_sector),
                         (2026100702, 0, 1, 2, 1))
        self.assertEqual(len(selector), updater.OTA_SECTOR_SIZE)
        self.assertEqual(updater.parse_ota_entry(selector, 1)["sequence"], 2)

    def test_recent_installation_updates_from_app1_to_app0(self):
        for installed in (2026100808, 2026100809, 2026100810):
            with self.subTest(installed=installed):
                flash = bytearray(b"\xff" * updater.FLASH_SIZE)
                image = f"\xe9BUDDY:WS154V2-BW:{installed}UL".encode("latin1")
                flash[updater.APP_OFFSETS[1]:updater.APP_OFFSETS[1] + len(image)] = image
                flash[updater.OTA_DATA:updater.OTA_DATA + updater.OTA_ENTRY.size] = \
                    updater.OTA_ENTRY.pack(1, bytes(20), 2, updater.ota_crc(1))
                second = updater.OTA_DATA + updater.OTA_SECTOR_SIZE
                flash[second:second + updater.OTA_ENTRY.size] = \
                    updater.OTA_ENTRY.pack(2, bytes(20), 2, updater.ota_crc(2))

                current, active_slot, target_slot, sequence, sector, selector = \
                    updater.prepare_update(flash, 2026100811)
                self.assertEqual((current, active_slot, target_slot, sequence, sector),
                                 (installed, 1, 0, 3, 0))
                self.assertEqual(updater.parse_ota_entry(selector, 0)["sequence"], 3)

    def test_refuses_pending_boot_validation_and_unknown_ota_state(self):
        for state in (1, 0x55):
            flash = bytearray(b"\xff" * updater.FLASH_SIZE)
            image = b"\xe9BUDDY:WS154V2-BW:2026100702UL"
            flash[updater.APP_OFFSETS[0]:updater.APP_OFFSETS[0] + len(image)] = image
            raw = updater.OTA_ENTRY.pack(1, bytes(20), state, updater.ota_crc(1))
            flash[updater.OTA_DATA:updater.OTA_DATA + len(raw)] = raw
            with self.assertRaises(updater.UpdateError):
                updater.prepare_update(flash, 2026100806)

    def test_selector_readback_requires_esp_idf_confirmed_state(self):
        sequence = 8
        expected = bytearray(b"\xff" * updater.OTA_SECTOR_SIZE)
        expected[:updater.OTA_ENTRY.size] = updater.make_ota_entry(sequence)
        confirmed = bytearray(expected)
        struct.pack_into("<I", confirmed, 24, 2)  # ESP_OTA_IMG_VALID
        aborted = bytearray(expected)
        struct.pack_into("<I", aborted, 24, 4)  # ESP_OTA_IMG_ABORTED

        self.assertTrue(updater.selector_readback_matches(bytes(expected), bytes(confirmed), sequence))
        self.assertFalse(updater.selector_readback_matches(bytes(expected), bytes(expected), sequence))
        self.assertFalse(updater.selector_readback_matches(bytes(expected), bytes(aborted), sequence))

    def test_write_verification_retries_a_transient_bad_read(self):
        payload = b"selector sector"
        read_attempts = []

        def fake_esptool(_base, _port, operation, *args, **kwargs):
            if operation == "read-flash":
                read_attempts.append(kwargs["baud"])
                Path(args[-1]).write_bytes(b"wrong" if len(read_attempts) == 1 else payload)

        with patch.object(updater, "run_esptool", side_effect=fake_esptool):
            updater.write_and_verify(["esptool"], "/dev/cu.usbmodem1", 460800,
                                     0xF000, payload, "ota-selector")

        self.assertEqual(read_attempts, [460800, 115200])

    def test_write_verification_reports_uncertain_write_after_all_bad_reads(self):
        payload = b"selector sector"
        read_attempts = []

        def fake_esptool(_base, _port, operation, *args, **kwargs):
            if operation == "read-flash":
                read_attempts.append(kwargs["baud"])
                Path(args[-1]).write_bytes(b"wrong")

        with patch.object(updater, "run_esptool", side_effect=fake_esptool):
            with self.assertRaisesRegex(updater.UpdateError, "write may have reached flash"):
                updater.write_and_verify(["esptool"], "/dev/cu.usbmodem1", 460800,
                                         0xF000, payload, "ota-selector")

        self.assertEqual(read_attempts, [460800, 115200, 115200])

    def test_runs_update_without_extra_confirmation_and_writes_inactive_ranges(self):
        output = ("Chip type: ESP32-S3-PICO-1\n"
                  "Features: Embedded Flash 8MB, Embedded PSRAM 8MB\n"
                  "MAC: 00:11:22:33:44:66\n")
        calls = []
        with tempfile.TemporaryDirectory() as directory:
            backup = Path(directory) / "flash.bin"
            flash = bytearray(b"\xff" * updater.FLASH_SIZE)
            table = (updater.BUNDLED_IMAGE.parent/"partitions.bin").read_bytes()
            flash[0x8000:0x8000+len(table)] = table
            backup.write_bytes(flash)
            def fake_run(_base, _port, operation, **_kwargs):
                calls.append(operation)
                if operation == "flash-id": return output
                if operation == "get-security-info":
                    return "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
                return ""

            writes = []
            with patch.object(updater, "esptool_command", return_value=["esptool"]), \
                 patch.object(updater, "discover_port", return_value="/dev/cu.usbmodem1"), \
                 patch.object(updater, "run_esptool", side_effect=fake_run), \
                 patch.object(updater, "read_full_backup", return_value=(backup, 460800)), \
                 patch.object(updater, "save_restore_receipt"), \
                 patch.object(updater, "prepare_update", return_value=(2026100702, 0, 1, 2, 1, b"selector")), \
                 patch.object(updater, "write_and_verify",
                              side_effect=lambda _base, _port, _baud, address, _data, label, **_kwargs:
                              writes.append((address, label))), \
                 patch.object(updater,"wait_for_boot_validation") as validation, \
                 patch("builtins.input", side_effect=AssertionError("updater must not prompt")):
                self.assertEqual(updater.main([]), 0)
                validation.assert_called_once_with("/dev/cu.usbmodem1",updater.parse_package(updater.PACKAGE)[0])

                # Flash/selector verification alone must not report success
                # if the final restart leaves the app unresponsive.
                validation.side_effect=updater.UpdateError("post-restart app unresponsive")
                with self.assertRaisesRegex(updater.UpdateError,"post-restart app unresponsive"):
                    updater.main([])

        self.assertEqual(calls, ["flash-id", "get-security-info"]*2)
        self.assertEqual(writes, [(updater.APP_OFFSETS[1], "app"),
                                  (updater.OTA_DATA + updater.OTA_SECTOR_SIZE, "ota-selector")]*2)



    def test_return_to_previous_app_changes_only_the_selector_plan(self):
        flash = bytearray(b"\xff" * updater.FLASH_SIZE)
        old, new = 2026101305, 2026101601
        previous = updater.APP_OFFSETS[0]
        current = updater.APP_OFFSETS[1]
        flash[previous:previous+len(f"\xe9BUDDY:WS154V2-BW:{old}UL")] = f"\xe9BUDDY:WS154V2-BW:{old}UL".encode("latin1")
        active_image = f"\xe9BUDDY:WS154V2-BW:{new}UL".encode("latin1")
        flash[current:current+len(active_image)] = active_image
        second = updater.OTA_DATA + updater.OTA_SECTOR_SIZE
        flash[second:second+updater.OTA_ENTRY.size] = updater.OTA_ENTRY.pack(2, bytes(20), 2, updater.ota_crc(2))
        snapshot = bytes(flash)
        with tempfile.TemporaryDirectory() as directory:
            receipt = Path(directory)/"receipt.json"
            updater.save_restore_receipt(receipt, flash, "board-one", old, 0, new)
            version, seq, sector, selector = updater.prepare_restore(flash, receipt, "board-one")
            self.assertEqual((version, seq, sector), (old, 3, 0))
            self.assertEqual(updater.parse_ota_entry(selector, sector)["sequence"],3)
            self.assertEqual(bytes(flash), snapshot)
            with self.assertRaisesRegex(updater.UpdateError,"another Buddy"):
                updater.prepare_restore(flash, receipt, "board-two")
            flash[previous+100] ^= 1
            with self.assertRaisesRegex(updater.UpdateError,"previous app slot changed"):
                updater.prepare_restore(flash, receipt, "board-one")

    def test_wrong_partition_layout_is_rejected_before_writing(self):
        with self.assertRaisesRegex(updater.UpdateError,"Partition table differs"):
            updater.validate_partition_layout(bytes(updater.FLASH_SIZE))

    def test_detects_windows_and_linux_ports(self):
        for device in ("COM7", "/dev/ttyACM0", "/dev/ttyUSB0"):
            port = Mock(device=device,vid=0x303a)
            with patch("serial.tools.list_ports.comports",return_value=[port]):
                self.assertEqual(updater.discover_port(),device)


    def test_restore_main_writes_only_alternate_selector_and_checks_boot(self):
        flash=bytearray(b"\xff"*updater.FLASH_SIZE)
        old,new=2026101305,2026101601
        for slot,release in ((0,old),(1,new)):
            image=f"\xe9BUDDY:WS154V2-BW:{release}UL".encode("latin1")
            at=updater.APP_OFFSETS[slot]
            flash[at:at+len(image)]=image
        table=(updater.BUNDLED_IMAGE.parent/"partitions.bin").read_bytes()
        flash[0x8000:0x8000+len(table)]=table
        at=updater.OTA_DATA+updater.OTA_SECTOR_SIZE
        flash[at:at+updater.OTA_ENTRY.size]=updater.OTA_ENTRY.pack(2,bytes(20),2,updater.ota_crc(2))
        hardware=("Chip type: ESP32-S3-PICO-1\nFeatures: Embedded Flash 8MB, Embedded PSRAM 8MB\n"
                  "MAC: 00:11:22:33:44:66\n")
        with tempfile.TemporaryDirectory() as directory:
            backups=Path(directory)
            backup=backups/"flash.bin";backup.write_bytes(flash)
            updater.save_restore_receipt(backups/"001122334466-previous.json",
                                         flash,"001122334466",old,0,new)
            writes=[]
            def read(_base,_port,operation,**kwargs):
                return hardware if operation=="flash-id" else "Secure Boot: Disabled\nFlash Encryption: Disabled\n"
            with patch.object(updater,"BACKUPS",backups), \
                 patch.object(updater,"esptool_command",return_value=["esptool"]), \
                 patch.object(updater,"discover_port",return_value="COM7"), \
                 patch.object(updater,"run_esptool",side_effect=read), \
                 patch.object(updater,"read_full_backup",return_value=(backup,115200)), \
                 patch.object(updater,"write_and_verify",side_effect=lambda _b,_p,_baud,address,*a,**k:writes.append(address)), \
                 patch.object(updater,"wait_for_boot_validation") as validation:
                self.assertEqual(updater.main(["--restore-previous-app"]),0)
                self.assertEqual(writes,[updater.OTA_DATA])
                validation.assert_called_once_with("COM7",old)

if __name__ == "__main__":
    unittest.main()
