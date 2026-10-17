import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from browser_package import SOURCE, package_compliance, verify_compliance
from assemble_source import assemble
from verify_public_export import verify_browser_limits, verify_manual


class BrowserPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='Buddy source with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.data=b'exact corresponding source bytes'*3
        (self.root/SOURCE).write_bytes(self.data)
        path=self.root/'notices/arduino-esp32/LICENSE';path.parent.mkdir(parents=True)
        path.write_text('Exact original notice\n')
        inventory={'sources':[{'name':'arduino-esp32',
                  'archive_sha256':hashlib.sha256(self.data).hexdigest(),'notices':['LICENSE']}]}
        (self.root/'dependency-inventory.json').write_text(json.dumps(inventory))
        with patch('browser_package.PART_BYTES',32):
            package_compliance(self.root,TOOLS/'assemble_source.py')

    def test_lossless_source_and_notice_transport(self):
        verify_compliance(self.root)
        self.assertFalse((self.root/SOURCE).exists())
        self.assertFalse((self.root/'notices').exists())
        with zipfile.ZipFile(self.root/'notices.zip') as archive:
            self.assertEqual(archive.read('notices/arduino-esp32/LICENSE'),b'Exact original notice\n')
        output=assemble(self.root)
        self.assertEqual(output.read_bytes(),self.data)
        self.assertEqual(assemble(self.root),output)

    def test_corrupted_missing_or_reordered_piece_fails_without_output(self):
        path=self.root/(SOURCE+'.part01');original=path.read_bytes()
        path.write_bytes(b'damaged')
        with self.assertRaisesRegex(ValueError,'Damaged'): assemble(self.root)
        self.assertFalse((self.root/SOURCE).exists())
        path.unlink()
        with self.assertRaisesRegex(ValueError,'Missing'): assemble(self.root)
        path.write_bytes(original)
        manifest=json.loads((self.root/'source-parts.json').read_text())
        manifest['parts'].reverse()
        (self.root/'source-parts.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'name/order'): assemble(self.root)
        self.assertFalse(list(self.root.glob('.buddy-source-*')))

    def test_does_not_overwrite_unrelated_output(self):
        target=self.root/'someone-elses-archive.tar.gz';target.write_bytes(b'keep me')
        with self.assertRaisesRegex(ValueError,'already exists'): assemble(self.root,target)
        self.assertEqual(target.read_bytes(),b'keep me')

    def test_missing_notice_or_wrong_full_source_hash_fails(self):
        manifest=json.loads((self.root/'source-parts.json').read_text())
        manifest['sha256']='0'*64
        (self.root/'source-parts.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'Full corresponding'): verify_compliance(self.root)
        manifest['sha256']=hashlib.sha256(self.data).hexdigest()
        (self.root/'source-parts.json').write_text(json.dumps(manifest))
        with zipfile.ZipFile(self.root/'notices.zip','w'): pass
        with self.assertRaisesRegex(ValueError,'notice'): verify_compliance(self.root)


class BrowserGateTests(unittest.TestCase):
    def test_public_source_file_count_and_size_limits(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            for i in range(500): (root/str(i)).touch()
            self.assertEqual(len(verify_browser_limits(root)),500)
            (root/'one-too-many').touch()
            with self.assertRaisesRegex(ValueError,'500 files'): verify_browser_limits(root)
            (root/'one-too-many').unlink()
            with (root/'0').open('wb') as stream: stream.truncate(25*1024*1024+1)
            with self.assertRaisesRegex(ValueError,'25 MiB'): verify_browser_limits(root)

    def test_cache_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);(root/'.DS_Store').touch()
            with self.assertRaisesRegex(ValueError,'runtime data'): verify_browser_limits(root)

    def test_real_manuals_have_portable_embedded_images(self):
        for path in (TOOLS.parent/'docs/manuals').glob('*.docx'):
            verify_manual(path)


if __name__=='__main__': unittest.main()
