"""Check that every bundled clip matches its source and survives compression."""
import importlib.util
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("buddy_build_sounds", ROOT / "build_sounds.py")
sound = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sound)


def decode(first, packed, count):
    predictor, index = first, 0
    result = [first]
    for frame in range(1, count):
        byte = packed[(frame - 1) // 2]
        code = byte >> 4 if frame % 2 == 0 else byte & 15
        step = sound.STEPS[index]
        change = step >> 3
        if code & 4: change += step
        if code & 2: change += step >> 1
        if code & 1: change += step >> 2
        predictor = max(-32768, min(32767, predictor - change if code & 8 else predictor + change))
        index = max(0, min(88, index + sound.INDEX_CHANGE[code & 7]))
        result.append(predictor)
    return result


class SoundAssetTests(unittest.TestCase):
    def test_every_clip_is_bundled_and_audibly_faithful(self):
        header = (ROOT / "sounds_generated.h").read_text()
        for name in sound.SOURCES:
            symbol = "buddy_adpcm_" + name.lower().replace("-", "_")
            source = sound.convert(name)
            encoded = sound.encode(source)
            match = re.search(rf"static const uint8_t {symbol}\[\] PROGMEM = \{{(.*?)\}};", header, re.S)
            self.assertIsNotNone(match, symbol)
            bundled = tuple(int(value) for value in re.findall(r"\d+", match.group(1)))
            self.assertEqual(bundled, encoded, symbol)
            self.assertIn(f"{symbol}_first = {source[0]};", header)
            self.assertIn(f"{symbol}_frames = {len(source)};", header)
            decoded = decode(source[0], encoded, len(source))
            signal = sum(sample * sample for sample in source)
            error = sum((a - b) ** 2 for a, b in zip(source, decoded))
            self.assertLess(error / signal, 0.20, symbol)


if __name__ == "__main__":
    unittest.main()
