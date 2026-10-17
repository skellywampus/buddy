# LGPL library relinking (source-private Buddy firmware)

Buddy application source is proprietary and is not included in this freeware
distribution. The Arduino-ESP32 core/libraries are LGPL-covered. Recipients
must retain the rights granted by those library licenses; the Buddy Freeware
License permits the limited changes/reverse engineering needed to modify an
LGPL-covered library and relink it with Buddy for the recipient's own use.

## Required release materials

For each firmware release, this directory's matching compliance package must
include:

- the complete corresponding source archive for the LGPL-covered Arduino
  library, including the exact version and any modifications;
- Buddy application object files compiled from the exact source/build used for
  the distributed image, plus needed application data;
- the matching toolchain, link configuration and any scripts needed to relink;
- a documented and tested process that replaces a library with a modified
  compatible version and produces a usable firmware image; and
- all applicable LGPL notices and license text.

The source archive by itself is not enough, and the ESP32 `.bin` or `.elf` is
not a substitute for the application object files. Release 2026101701 supplies
all items above in its `compliance` directory. Its clean relink reproduced the
released application payload, and a second test with a modified Arduino-ESP32
Preferences library relinked successfully and changed the output. No board was
flashed for these tests.

In the public browser-upload package the complete Arduino archive is carried
as numbered `.partNN` files. Run `python3 assemble_source.py` in the compliance
folder to reconstruct and verify it without a download. The original notices
are bundled in `notices.zip`; unzip that file to inspect their directory tree.
This changes transport packaging only, not any source, notice, application
object, or firmware byte.

## Relinking procedure

The release's `compliance/relink/relink.py` accepts the exact application
objects and a locally modified Arduino library directory, builds a discovery
sketch with the pinned FQBN, and reuses its generated linker command. It does
not flash a board. It verifies object hashes and the installed toolchain before
compiling, and emits a validated `.bin` alongside the `.elf`. See the
[release-specific instructions](../releases/2026101701/compliance/relink/RELINK.md)
and its acceptance-test report. The report is bound to the image, object
manifest, helper script and dependency inventory, so it cannot be reused after
those artifacts change.

The release artifacts and test report are version-specific; do not substitute
objects from another build. See [LICENSE-SCOPE.md](../LICENSE-SCOPE.md).
