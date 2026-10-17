from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from private_storage import backup_directory, restore_receipt


class PrivateStorageTests(unittest.TestCase):
    def test_platform_paths_are_outside_download_folder(self):
        home=Path('/private/tmp/buddy-example-home')
        with patch('private_storage.Path.home',return_value=home),patch.dict('os.environ',{},clear=True):
            for platform,expected in [('darwin',home/'Library/Application Support/Buddy/backups'),
                                      ('win32',home/'AppData/Local/Buddy/backups'),
                                      ('linux',home/'.local/share/Buddy/backups')]:
                with patch('private_storage.sys.platform',platform): self.assertEqual(backup_directory(),expected)
            with patch('private_storage.sys.platform','linux'),patch.dict('os.environ',{'XDG_DATA_HOME':'/private/tmp/data'}):
                self.assertEqual(backup_directory(),Path('/private/tmp/data/Buddy/backups'))
            with patch('private_storage.sys.platform','linux'),patch.dict('os.environ',{'XDG_DATA_HOME':'relative'}):
                self.assertEqual(backup_directory(),home/'.local/share/Buddy/backups')

    def test_legacy_restore_receipt_fallback_but_new_receipt_takes_priority(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)/'download';backups=Path(td)/'private';backups.mkdir()
            legacy=root/'build/backups/id-previous.json';legacy.parent.mkdir(parents=True);legacy.write_text('{}')
            current=backups/'id-previous.json'
            self.assertEqual(restore_receipt(root,backups,'id'),legacy)
            current.write_text('{}')
            self.assertEqual(restore_receipt(root,backups,'id'),current)


if __name__=='__main__': unittest.main()
