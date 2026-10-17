# Build and relink Buddy

Buddy's application source is included under the repository's MIT license.
This page covers ordinary builds and the additional steps relevant to
third-party LGPL libraries in the binary release.

## Build from source

Follow [developer setup](DEVELOPMENT.md) to install the pinned Arduino-ESP32
toolchain and build for the supported Waveshare V2 monochrome non-touch board.
The build and package tools do not flash hardware. The source archive, build
configuration, sound assets, and deterministic host tests are part of this
repository.

For a matching release package, run the documented build and compliance
commands in [PUBLISHING.md](PUBLISHING.md). Keep the release's exact dependency
inventory and third-party notices with its firmware binaries.

## LGPL-covered Arduino libraries

The firmware links Arduino-ESP32 components under LGPL-2.1-or-later with
file-specific exceptions. The release package includes the corresponding
library source, license texts, and notices. You may modify LGPL-covered
libraries and relink them with Buddy subject to those licenses. Preserve
upstream notices and provide corresponding source for any modified library
when distributing the result.

The release also includes exact application object files and a tested helper
for relinking a modified library against the matching application. This is an
additional route, not a restriction on using the public Buddy source to build
and relink a complete modified firmware. Follow the release-specific
instructions under `releases/2026101701/compliance/relink/` when using those
objects; do not mix them with a different release or build.

## Binary components

Some Espressif speech/runtime components and models are distributed as
prebuilt binaries under Espressif-specific terms and are not all
source-available. Their licenses are not replaced by Buddy's MIT license.
See [THIRD_PARTY.md](../THIRD_PARTY.md).
