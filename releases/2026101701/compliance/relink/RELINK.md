# Relinking Buddy with modified Arduino libraries

This folder contains the two application object files used to build the release.
`object-manifest.json` identifies the release, board, build timestamp, exact
object hashes, and matching firmware image. Buddy's application source remains
private. The accompanying Arduino source and third-party notices are in
the parent compliance folder. In the public download, the exact source archive
is split into numbered pieces so each file fits GitHub's browser-upload limit;
no source has been omitted. From the compliance folder, run:

```sh
python3 assemble_source.py
```

On Windows, use `py -3 assemble_source.py`. The script uses only Python's
standard library, requires no network, verifies each piece and the complete
archive, and creates `arduino-esp32-3.3.12-source.tar.gz`. `source-parts.json`
records the exact hashes. Open `notices.zip` to read the full original
third-party notice collection, preserving its directory paths. The private
development staging tree also retains the unsplit archive and notice directory.

Install Python 3.10 or newer, Arduino CLI, and the pinned board package:

```sh
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
```

The helper requires Arduino-ESP32 3.3.12, its ESP32-S3 SDK 3.3.12, and esp-x32
2601. It verifies the supplied object files before running the compiler.

Extract the accompanying Arduino source archive to inspect or modify the
LGPL-covered source. For a library such as Preferences, keep its folder layout
(`Preferences/library.properties` and `Preferences/src/`) under a directory
of modified libraries. Pass that directory with `--libraries`. For changes to
the Arduino core itself, apply the changes to the installed 3.3.12 core source;
the helper compiles a fresh core archive on each run.

From this relink directory, run:

```sh
python3 relink.py --objects . --output Buddy-relinked.elf
python3 relink.py --objects . --libraries "/path/to/modified libraries" --output Buddy-modified.elf
```

The helper obtains the board's linker and image-converter commands from Arduino
CLI, substitutes the exact application objects, and generates both `.elf` and
`.bin` outputs. It validates the image checksum, appended SHA-256 digest,
ESP32-S3 target, firmware release marker, and application-partition size.
It does not upload, erase, or flash a device. A raw `.bin` is not an SD update
package; installation of a modified image is a developer operation using the
existing partition layout and an appropriate recovery backup.

`relink-test.json` records clean and modified-library acceptance tests. The clean
test compares every application payload byte after normalizing only the ELF
digest and regenerated image integrity fields. The modified test recompiles
the supplied LGPL Preferences source with a deliberate change to `putBytes`,
checks that the modified library was selected, and verifies that it changed
the resulting payload. The report is tied to the firmware, object manifest,
helper script, and dependency inventory; changing those requires new tests.
Tests run on macOS. Native Windows and Linux relinking have not been exercised
on their respective operating systems in this release review.

The relink helper is MIT licensed under the root `INSTALLER-TOOLS-LICENSE`.
The application objects retain the root Buddy Freeware License and its LGPL
relinking exception. Upstream libraries retain their respective licenses.
