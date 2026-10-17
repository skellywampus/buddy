"""Offline manifest/package and local Markdown link checks. Never opens USB."""
import hashlib
import json
from pathlib import Path
import re
import struct
from urllib.parse import unquote, urlsplit
from verify_relink import verify_relink

ROOT = Path(__file__).resolve().parents[1]
VERSION = ROOT / "firmware/Buddy/buddy_update_format.h"


def verify():
    release = int(re.search(r"^#define BUDDY_FIRMWARE_VERSION (\d+)UL$",
                            VERSION.read_text(), re.M).group(1))
    folder = ROOT / "releases" / str(release)
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["release"] == release and manifest["flash_size"] == 0x800000
    expected = {"bootloader.bin": 0, "partitions.bin": 0x8000,
                "boot_app0.bin": 0xE000, "Buddy.ino.bin": 0x10000,
                "srmodels.bin": 0x4B0000}
    assert len(manifest["images"]) == len(expected)
    seen = set()
    for item in manifest["images"]:
        name = item["file"]
        assert name in expected and name not in seen and int(item["address"], 16) == expected[name]
        seen.add(name)
        data = (folder / "first-install" / name).read_bytes()
        assert 0 < len(data) <= item["max_size"], name
        assert hashlib.sha256(data).hexdigest() == item["sha256"], name
    app = (folder / "first-install/Buddy.ino.bin").read_bytes()
    assert app[0] == 0xE9 and app[12:14] == b"\x09\x00" and len(app) <= 0x250000
    assert f"BUDDY:WS154V2-BW:{release}UL".encode() in app
    package = (ROOT / "BUDDY.UPD").read_bytes()
    assert package == (folder / "BUDDY.UPD").read_bytes()
    magic, format_, target, number, size, digest = struct.unpack("<8sI16sII32s", package[:68])
    assert (magic, format_, target.rstrip(b"\0"), number, size) == (
        b"BUDDYUP1", 1, b"WS154V2-BW", release, len(app))
    assert package[68:] == app and hashlib.sha256(app).digest() == digest
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        checksum, name = line.split(None, 1)
        local = (folder / name.strip()).resolve()
        assert local.is_relative_to(folder) and hashlib.sha256(local.read_bytes()).hexdigest() == checksum
    for image in (folder / "first-install").glob("*.bin"):
        data = image.read_bytes()
        assert not re.search(rb"/Users/[^/\x00]+/", data), image.name
    # The package includes the exact corresponding Arduino library source and
    # a hashed notice collection for this build, rather than a moving source link.
    compliance = folder / "compliance"
    lock = json.loads((ROOT/"tools/dependencies.lock.json").read_text())
    core = next(x for x in lock["sources"] if x["name"] == "arduino-esp32")
    assert hashlib.sha256((compliance/"arduino-esp32-3.3.12-source.tar.gz").read_bytes()).hexdigest() == core["archive_sha256"]
    inventory = json.loads((compliance/"dependency-inventory.json").read_text())
    assert inventory["release"] == release and inventory["archives"]
    assert inventory["application_sha256"] == hashlib.sha256(app).hexdigest(), "Stale dependency inventory"
    assert inventory["speech_model_sha256"] == manifest["images"][-1]["sha256"]
    verify_relink(folder, release)
    assert inventory["sources"] == [dict(source, url=f"https://github.com/{source['repo']}/tree/{source['commit']}",
                                        notices=entry["notices"])
                                    for source,entry in zip(lock["sources"],inventory["sources"])]
    assert len(inventory["sources"]) == len(lock["sources"])
    for source in inventory["sources"]:
        assert source["notices"]
        for name in source["notices"]:
            assert (compliance/"notices"/source["name"]/name).is_file()
    source_set = set()
    for line in (folder/"SOURCE-SHA256SUMS").read_text().splitlines():
        checksum, name = line.split(None,1)
        path = (ROOT/name.strip()).resolve()
        assert path.is_relative_to(ROOT) and hashlib.sha256(path.read_bytes()).hexdigest() == checksum, name
        source_set.add(path)
    current_sources = {p.resolve() for d in ("firmware","third_party") for p in (ROOT/d).rglob("*")
                       if p.is_file() and "__pycache__" not in p.parts and not p.name.endswith(".pyc")}
    assert source_set == current_sources, "Source tree differs from the packaged release"
    links = 0
    documents = list(ROOT.glob("*.md"))
    for directory in ("docs", ".github", "LICENSES"):
        documents.extend((ROOT/directory).rglob("*.md"))
    documents.extend((ROOT/"firmware").rglob("README.md"))
    documents.extend((ROOT/"third_party").rglob("PROVENANCE.md"))
    for document in documents:
        text = document.read_text()
        targets = re.findall(r"!?\[[^\]]*\]\(([^)\n]+)\)", text)
        targets += re.findall(r'<img\s+[^>]*src="([^"]+)"', text)
        for target in targets:
            parsed = urlsplit(target.strip("<>"))
            if parsed.scheme or not parsed.path:
                continue
            local = (document.parent / unquote(parsed.path)).resolve()
            assert local.is_relative_to(ROOT) and local.exists(), (document.relative_to(ROOT), target)
            links += 1
    print(f"PASS: release {release}, five images, matching SD package/source, {len(inventory['archives'])} linked archives, accompanying source/notices, {links} local links")


if __name__ == "__main__":
    verify()
