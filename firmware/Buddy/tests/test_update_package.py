import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "package_update.py"
spec = importlib.util.spec_from_file_location("buddy_package_update", SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class UpdatePackageTests(unittest.TestCase):
    def image(self):
        data = bytearray(256)
        data[0] = 0xE9
        data[12:14] = b"\x09\x00"  # ESP32-S3 image chip ID
        data.extend(f"BUDDY:WS154V2-BW:{module.release_number()}UL".encode())
        return bytes(data)

    def test_one_file_has_matching_header_and_digest(self):
        image = self.image()
        result = module.package(image)
        magic, format_, target, release, size, digest = struct.unpack(module.FORMAT, result[:68])
        self.assertEqual(magic, b"BUDDYUP1")
        self.assertEqual(format_, 1)
        self.assertEqual(target.rstrip(b"\0"), module.TARGET)
        self.assertEqual(release, module.release_number())
        self.assertEqual(size, len(image))
        self.assertEqual(result[68:], image)
        self.assertEqual(digest, hashlib.sha256(result[68:]).digest())
        altered = bytearray(result)
        altered[-1] ^= 1
        self.assertNotEqual(digest, hashlib.sha256(altered[68:]).digest())

    def test_rejects_wrong_or_unmarked_image(self):
        image = self.image()
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            module.package(b"not an ESP32 image")
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            module.package(image[:12] + b"\x00\x00" + image[14:])
        with self.assertRaisesRegex(ValueError, "marker"):
            module.package(image.replace(b"BUDDY:", b"OTHER:"))
        with self.assertRaisesRegex(ValueError, "fit"):
            module.package(image + bytes(module.LIMIT))


if __name__ == "__main__":
    unittest.main()
