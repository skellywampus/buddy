"""Verify release-specific relink artifacts and their acceptance-test binding."""
import json
from relink_release import load_manifest, sha


def bindings(folder):
    relink = folder / "compliance/relink"
    return {
        "application_sha256": sha(folder / "first-install/Buddy.ino.bin"),
        "object_manifest_sha256": sha(relink / "object-manifest.json"),
        "relink_helper_sha256": sha(relink / "relink.py"),
        "dependency_inventory_sha256": sha(folder / "compliance/dependency-inventory.json"),
    }


def verify_relink(folder, release):
    relink = folder / "compliance/relink"
    required = ("object-manifest.json", "relink.py", "RELINK.md", "relink-test.json")
    for name in required:
        if not (relink / name).is_file() or (relink / name).is_symlink():
            raise ValueError("Missing relink release material: " + name)
    manifest = load_manifest(relink)
    expected = bindings(folder)
    if manifest["release"] != release or manifest["application_sha256"] != expected["application_sha256"]:
        raise ValueError("Relink objects do not match this firmware release")
    report = json.loads((relink / "relink-test.json").read_text())
    if (report.get("schema_version") != 1 or report.get("release") != release
            or report.get("status") != "PASS"
            or any(report.get(key) != value for key, value in expected.items())):
        raise ValueError("Relink test report is absent, stale, or bound to different artifacts")
    if report.get("tests") != {"clean_payload_matches": True, "modified_library_relinks": True,
                              "modified_library_changes_payload": True}:
        raise ValueError("Relink acceptance tests are incomplete")
    source = report.get("modified_library_source", {})
    if not source.get("original_sha256") or source.get("original_sha256") == source.get("modified_sha256"):
        raise ValueError("Modified-library source evidence is missing")
    return manifest
