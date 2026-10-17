# License scope and freeware distribution

Buddy is freeware, not open-source software. The current distribution model is
free download and use on the supported board, with Buddy firmware source,
artwork source, and original sound files kept private. The exact user
permissions and third-party exceptions are in [LICENSE](LICENSE). This file
describes scope; it does not replace that license or provide legal advice.

## Buddy-owned material

- Firmware binaries and Buddy-authored application code: Buddy Freeware License.
- Original Buddy artwork and sound clips: Buddy Freeware License; raw creative
  source assets are not included in this distribution.
- Manuals and user-facing documentation: may be downloaded and read for personal
  use under the Buddy Freeware License; no resale or redistribution is granted.
- First-flash, USB update/restore utilities, and the supplied relink and source-assembly helpers:
  separately MIT licensed in [INSTALLER-TOOLS-LICENSE](INSTALLER-TOOLS-LICENSE).

The MIT license formerly present at the repository root has been removed from
the freeware package. It must not be copied into new Buddy firmware releases.

## Third-party components

Third-party works keep their own licenses and notices. The freeware license
does not remove those rights or obligations. See [THIRD_PARTY.md](THIRD_PARTY.md),
the texts in [LICENSES/](LICENSES/), and the version-specific release
compliance directory. These include, among others, LGPL-covered Arduino-ESP32
components, Espressif components with hardware-specific terms, Pocket Scout
MIT-attributed helpers, the Waveshare reference material, and runtime notices.

## LGPL distribution condition

The firmware statically links Arduino-ESP32 components. A freeware binary may
be distributed without publishing Buddy's source only if the exact release is
accompanied by the complete corresponding source for the LGPL-covered library
and usable materials to modify that library and relink the application. The
Buddy Freeware License therefore grants the limited modification and
reverse-engineering permissions required for that purpose.

For a source-private release, the compliance package must contain application
object files produced from the exact release source/build, the needed link
configuration and data, and tested relinking instructions. **Do not publish a
binary release while those matching materials are absent or unverified.**
The Arduino source archive and license text alone are not sufficient. Do not
replace the object-file route with a claim that the Buddy source is required or
publicly available.

## Release gate

Before publishing a release, verify that:

1. The public export does not contain Buddy application source, raw sound
   sources, or private build history.
2. Its object/relink package matches that release's source identity and
   configuration and has been tested against a modified LGPL library.
3. All notices, corresponding source, binaries, and compliance instructions
   are included with the download and any distribution of assembled devices.
4. The EULA, user permissions, ownership of contributions, and any sales or
   transfer policy have been reviewed for the intended distribution.

The current release includes the exact relink objects and the tested helper.
The exporter binds the test report to the firmware, object manifest, helper,
and dependency inventory; changing any of those invalidates acceptance.
