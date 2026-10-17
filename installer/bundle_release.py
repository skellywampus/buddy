"""Find a packaged release without depending on private firmware source."""
from pathlib import Path
import struct

HEADER = struct.Struct("<8sI16sII32s")
TARGET = b"WS154V2-BW\0\0\0\0\0\0"


def release_folder(root: Path) -> Path:
    package = root / "BUDDY.UPD"
    if package.is_file():
        with package.open("rb") as stream:
            header = stream.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("BUDDY.UPD has an incomplete header")
        magic, fmt, target, release, size, _digest = HEADER.unpack(header)
        if (magic, fmt, target) != (b"BUDDYUP1", 1, TARGET) or not release:
            raise ValueError("BUDDY.UPD is not a supported Buddy release")
        if not 0 < size <= 0x250000 or package.stat().st_size != HEADER.size + size:
            raise ValueError("BUDDY.UPD has an invalid image size")
        return root / "releases" / str(release)
    candidates = [p for p in (root / "releases").glob("*")
                  if p.is_dir() and p.name.isdecimal() and (p / "manifest.json").is_file()]
    if len(candidates) != 1:
        raise ValueError("A single matched release or BUDDY.UPD is required")
    return candidates[0]
