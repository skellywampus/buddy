"""Regression checks against the folder an end user receives, without source."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer"))
from bundle_release import release_folder


class PublicBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="Buddy download with spaces ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(ROOT / "installer", self.root / "installer",
                        ignore=shutil.ignore_patterns("tests", "__pycache__"))
        shutil.copyfile(ROOT / "BUDDY.UPD", self.root / "BUDDY.UPD")
        release = release_folder(ROOT)
        public = self.root / "releases" / release.name
        public.mkdir(parents=True)
        shutil.copyfile(release / "manifest.json", public / "manifest.json")
        shutil.copytree(release / "first-install", public / "first-install")
        self.release = public

    def run_launcher(self, name, *flags):
        return subprocess.run([sys.executable,str(self.root/"installer"/name),*flags],
                              cwd=self.root,env={k:v for k,v in os.environ.items() if k!="PYTHONDONTWRITEBYTECODE"},
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)

    def test_source_free_install_update_restore_and_help(self):
        self.assertFalse((self.root/"firmware").exists())
        for name, flags in (("launch_first_flash.py",()),("launch_usb_update.py",()),
                            ("launch_usb_update.py",("--restore-previous-app",))):
            result=self.run_launcher(name,*flags,"--verify-files")
            self.assertEqual(result.returncode,0,result.stdout)
            self.assertIn("No board was touched",result.stdout)
        self.assertEqual(self.run_launcher("launch_usb_update.py","--help").returncode,0)
        self.assertFalse((self.root/"build").exists())
        self.assertFalse(list(self.root.rglob('__pycache__')))

    def test_corrupt_export_is_rejected_before_environment_or_hardware(self):
        (self.release/"first-install/Buddy.ino.bin").write_bytes(b"damaged")
        for name in ("launch_first_flash.py","launch_usb_update.py"):
            result=self.run_launcher(name,"--verify-files")
            self.assertNotEqual(result.returncode,0)
            self.assertNotIn("Traceback",result.stdout)
            self.assertNotIn("Setting up",result.stdout)
        self.assertFalse((self.root/"build").exists())

    def test_damaged_root_update_never_selects_another_release(self):
        (self.root/"BUDDY.UPD").write_bytes(b"damaged")
        with self.assertRaises(ValueError):
            release_folder(self.root)
        result=self.run_launcher("launch_usb_update.py","--help")
        self.assertEqual(result.returncode,0,result.stdout)


if __name__=="__main__": unittest.main()
