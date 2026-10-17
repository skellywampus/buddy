import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from relink_release import FQBN, image_payload, load_manifest, relink, sha
from verify_relink import bindings, verify_relink
from build_release import checksums


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)/"2026101701"
        self.objects=self.folder/"compliance/relink";self.objects.mkdir(parents=True)
        (self.folder/"first-install").mkdir()
        (self.folder/"first-install/Buddy.ino.bin").write_bytes(b"app")
        (self.folder/"compliance/dependency-inventory.json").write_text("{}")
        (self.objects/"relink.py").write_text("# helper\n")
        (self.objects/"RELINK.md").write_text("Instructions")
        entries=[]
        for name in ("Buddy.ino.cpp.o","buddy_esp32_sr.c.o"):
            path=self.objects/name;path.write_bytes(name.encode())
            entries.append({"file":name,"bytes":path.stat().st_size,"sha256":sha(path)})
        self.manifest={"release":2026101701,"application_sha256":sha(self.folder/"first-install/Buddy.ino.bin"),
                       "fqbn":FQBN,"arduino_core":"3.3.12","toolchain":"esp-x32 2601",
                       "build_date":"Oct 17 2026","build_time":"03:49:02","objects":entries}
        self.write_manifest(self.manifest)
        self.report={"schema_version":1,"release":2026101701,"status":"PASS",**bindings(self.folder),
                     "tests":{"clean_payload_matches":True,"modified_library_relinks":True,
                              "modified_library_changes_payload":True},
                     "modified_library_source":{"original_sha256":"a"*64,"modified_sha256":"b"*64}}
        (self.objects/"relink-test.json").write_text(json.dumps(self.report))

    def write_manifest(self,value):
        (self.objects/"object-manifest.json").write_text(json.dumps(value))

    def test_accepts_exact_bound_artifacts(self):
        verify_relink(self.folder,2026101701)

    def test_rejects_helper_or_inventory_change_under_same_release(self):
        for path in (self.objects/"relink.py",self.folder/"compliance/dependency-inventory.json"):
            before=path.read_bytes();path.write_bytes(before+b"\nchanged")
            with self.assertRaisesRegex(ValueError,"stale"):
                verify_relink(self.folder,2026101701)
            path.write_bytes(before)

    def test_empty_duplicate_traversal_objects_and_wrong_toolchain_fail(self):
        changes=[{"objects":[]},{"objects":[self.manifest["objects"][0]]*2},
                 {"objects":[{**self.manifest["objects"][0],"file":"../secret.o"},self.manifest["objects"][1]]},
                 {"arduino_core":"3.4.0"},{"toolchain":"different"}]
        for change in changes:
            self.write_manifest({**self.manifest,**change})
            with self.assertRaises(ValueError): load_manifest(self.objects)

    def test_modified_object_is_rejected_before_any_tool_runs(self):
        (self.objects/"Buddy.ino.cpp.o").write_bytes(b"changed")
        with patch("relink_release.shutil.which") as probe:
            with self.assertRaisesRegex(ValueError,"damaged"):
                relink(self.objects,Path(self.temp.name)/"out.elf")
            probe.assert_not_called()


class ImageValidationTests(unittest.TestCase):
    def setUp(self):
        self.image=(TOOLS.parent/"releases/2026101701/first-install/Buddy.ino.bin").read_bytes()

    def test_accepts_current_app_but_rejects_corruption(self):
        self.assertTrue(image_payload(self.image))
        for position in (0,1000,len(self.image)-1):
            data=bytearray(self.image);data[position]^=1
            with self.assertRaises(ValueError): image_payload(bytes(data))

    def test_normalizes_integrity_fields_without_masking_program_changes(self):
        def repair(data):
            end=len(data)-33
            # Changing a descriptor byte also changes the segment XOR checksum.
            checksum=0xEF;pos=24
            for _ in range(data[1]):
                size=int.from_bytes(data[pos+4:pos+8],"little");pos+=8
                for byte in data[pos:pos+size]: checksum^=byte
                pos+=size
            data[end]=checksum
            data[end+1:]=hashlib.sha256(data[:end+1]).digest()
            return bytes(data)
        digest_change=bytearray(self.image);digest_change[176]^=1
        self.assertEqual(image_payload(self.image),image_payload(repair(digest_change)))
        program_change=bytearray(self.image);program_change[1000]^=1
        self.assertNotEqual(image_payload(self.image),image_payload(repair(program_change)))


class ChecksumTests(unittest.TestCase):
    def test_generated_python_caches_do_not_enter_release_checksums(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            (root/"__pycache__").mkdir()
            (root/"__pycache__/helper.pyc").write_bytes(b"cache")
            (root/".DS_Store").write_bytes(b"finder")
            (root/"Buddy.ino.cpp.o").write_bytes(b"required object")
            checksums(root)
            manifest=(root/"SHA256SUMS").read_text()
            self.assertIn("Buddy.ino.cpp.o",manifest)
            self.assertNotIn("pycache",manifest)
            self.assertNotIn(".DS_Store",manifest)


if __name__=="__main__": unittest.main()
