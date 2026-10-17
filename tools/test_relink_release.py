#!/usr/bin/env python3
"""Accept the packaged relink workflow using matching and modified LGPL libraries."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import sys
from build_release import ROOT, release_number, checksums
from relink_release import sha
from verify_relink import bindings, verify_relink


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", type=int, default=release_number())
    args = parser.parse_args()
    folder = ROOT / "releases" / str(args.release)
    objects = folder / "compliance/relink"
    report_path = objects / "relink-test.json"
    report_path.unlink(missing_ok=True)
    spec = importlib.util.spec_from_file_location("packaged_buddy_relink", objects / "relink.py")
    helper = importlib.util.module_from_spec(spec)
    old_bytecode=sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode=True
        spec.loader.exec_module(helper)
    finally:
        sys.dont_write_bytecode=old_bytecode
    helper.load_manifest(objects)
    with tempfile.TemporaryDirectory(prefix="buddy acceptance with spaces ") as td:
        tmp = Path(td)
        libraries = tmp / "modified libraries"
        with tarfile.open(folder / "compliance/arduino-esp32-3.3.12-source.tar.gz") as archive:
            for member in archive.getmembers():
                parts = Path(member.name).parts
                if not member.isfile() or len(parts) < 4 or parts[1:3] != ("libraries", "Preferences"):
                    continue
                relative = Path(*parts[2:])
                if ".." in relative.parts:
                    raise ValueError("Invalid library source archive path")
                target = libraries / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
        source = libraries / "Preferences/src/Preferences.cpp"
        original = source.read_text()
        start = original.index("size_t Preferences::putBytes(")
        end = original.index("\n}", start)
        function = original[start:end]
        if function.count("return len;") != 1:
            raise ValueError("Test requires the pinned Preferences putBytes source")
        original_sha = sha(source)
        source.write_text(original[:start] + function.replace("return len;", "return len + 1;") + original[end:])
        modified_sha = sha(source)
        clean = helper.relink(objects, tmp / "clean.elf")
        altered = helper.relink(objects, tmp / "modified.elf", libraries)
        released = helper.image_payload((folder / "first-install/Buddy.ino.bin").read_bytes())
        baseline = helper.image_payload((tmp / "clean.bin").read_bytes())
        modified = helper.image_payload((tmp / "modified.bin").read_bytes())
        if released != baseline:
            raise ValueError("Clean relink application payload differs from the released image")
        if modified == baseline or str(libraries / "Preferences") not in altered["build_log"]:
            raise ValueError("Modified LGPL library was not selected or did not change the payload")
        report = {
            "schema_version": 1, "release": args.release, "status": "PASS",
            **bindings(folder), "tested_at": datetime.now(timezone.utc).isoformat(),
            "tests": {"clean_payload_matches": True, "modified_library_relinks": True,
                      "modified_library_changes_payload": True},
            "clean_image_sha256": clean["image_sha256"],
            "modified_image_sha256": altered["image_sha256"],
            "modified_library_source": {"file": "Preferences/src/Preferences.cpp",
                                        "original_sha256": original_sha, "modified_sha256": modified_sha},
            "hardware_touched": False,
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    verify_relink(folder, args.release)
    checksums(folder)
    print("PASS: packaged clean and modified-library relinks; report bound to exact release artifacts")


if __name__ == "__main__":
    main()
