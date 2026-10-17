# Release 2026101701 accompanying materials

This directory accompanies the firmware images and BUDDY.UPD in this release.
Keep it with all firmware downloads and any distribution of a Buddy device.

- The public download supplies the complete pinned Arduino core/library source
  in four numbered `arduino-esp32-3.3.12-source.tar.gz.partNN` files.
  Run `python3 assemble_source.py` here (Windows: `py -3 assemble_source.py`)
  to reconstruct the original `.tar.gz` without network access. The helper
  verifies every piece and the full archive against `source-parts.json` and
  refuses to replace an unrelated existing file. The private development
  staging tree retains the same archive unsplit.
- `dependency-inventory.json` identifies the 83 archive inputs selected by
  the linker, their hashes, Arduino libraries, upstream source commits and
  notices.
- `notices.zip` in the public download contains all 67 SDK/runtime notice files
  with their original paths. `notices-index.json` records each file's hash.
  Open the ZIP normally or extract it to recreate the `notices` directory.
  These files contain the corresponding SDK/runtime component grants and
  copyright notices. Some notices cover a broader upstream source distribution
  than the features present in this firmware.

Buddy firmware is proprietary freeware under the root LICENSE, not MIT. The
Arduino library retains LGPL-2.1-or-later and its per-file exceptions; speech,
model and other components retain their own terms. The Espressif speech grant
is limited to Espressif hardware. The combined firmware is not a single-license
MIT binary.

This release includes the two application object files matching the release
image, a pinned relinking helper, their hashes, the LGPL library source archive,
and the required notices. The helper was tested both as a clean relink (its
application payload matched the released image) and with a deliberately
modified LGPL Preferences library (the relink succeeded and changed the output).
No board was flashed during testing. See `relink/RELINK.md`,
[notices](../../../NOTICE.md), and [license scope](../../../LICENSE-SCOPE.md).
