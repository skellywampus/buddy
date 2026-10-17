# Buddy application source

The full project introduction is in [the root README](../../README.md). Source version: `2026101701`.

- [Developer build, architecture and save invariants](../../docs/DEVELOPMENT.md)
- [Hardware, pins, power latch and Cable Friends safety](../../docs/HARDWARE.md)
- [First USB provisioning](../../FIRST-INSTALL.md)
- [Ordinary microSD updates](../../UPDATE.md)
- [Release notes](../../CHANGELOG.md)
- [Licensing scope](../../LICENSE-SCOPE.md)

Buddy-authored firmware code and original sound assets are covered by the
repository's MIT License. Files carrying third-party headers retain their
upstream licenses; see [third-party notices](../../THIRD_PARTY.md).

Keep this directory, its sound assets and vendor support in their checked-in relative locations. Shared hardware and sensor helpers live directly in this Buddy firmware directory. This source is the normal production life model, not a six-hour test build. Tests can enable accelerated time at compile time without flashing any hardware.
