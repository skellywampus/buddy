"""Ensure the carried licensed reference and copied data have exact provenance."""
import hashlib
from pathlib import Path
import re
import unittest

DRIVER=Path(__file__).resolve().parents[3]/"third_party/waveshare_epaper_display"
class DisplayProvenance(unittest.TestCase):
    def test_upstream_snapshot_and_waveforms(self):
        source=(DRIVER/"upstream/epd1in54_V2.cpp.txt").read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),
                         "6a28e4c77c07a8a143ff971146a31ef03e9727c4b0a5badfa58f2da06c2efed6")
        reference=source.decode()
        tables=(DRIVER/"epaper_waveforms.h").read_text()
        for name in ("WF_Full_1IN54","WF_PARTIAL_1IN54_0"):
            def values(text):
                data=re.search(name+r"\[159\]\s*=\s*\{([^}]+)\}",text).group(1)
                return [int(v.strip(),0) for v in data.split(",") if v.strip()]
            self.assertEqual(values(reference),values(tables))
            self.assertEqual(len(values(tables)),159)
        self.assertIn("Permission is hereby granted",tables)
        self.assertIn("Waveshare team",tables)
