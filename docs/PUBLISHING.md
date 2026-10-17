# Build, verify, and publish Buddy

The public repository contains Buddy's MIT-licensed application source,
firmware artwork and sound assets, deterministic tests, documentation, and
build/installer tools. Third-party components retain their own terms; see
[THIRD_PARTY.md](../THIRD_PARTY.md). The public export deliberately excludes
private device data, old private Git history, backups, logs, caches, and
accelerated-life test builds.

## Verify the current source

From the repository root:

```sh
bash firmware/Buddy/tests/run_host_tests.sh
python3 -m unittest discover -s tools/tests -p 'test_*.py'
python3 tools/verify_release.py
```

For a firmware build, install Arduino CLI and the pinned
Arduino-ESP32 3.3.12 toolchain, then follow [DEVELOPMENT.md](DEVELOPMENT.md).
The build/package scripts never flash a connected device. Hardware testing
must be described separately and must not be inferred from a successful
compile.

## Prepare a release

Use this order so the binary, manifest, dependency inventory, notices, and
corresponding-source package refer to the same build:

```sh
python3 tools/build_release.py --build-path build/esp32s3
python3 tools/prepare_compliance.py --build-path build/esp32s3
python3 tools/test_relink_release.py
bash firmware/Buddy/tests/run_host_tests.sh
python3 -m unittest discover -s tools/tests -p 'test_*.py'
python3 tools/export_github.py
```

When reusing a previously pinned dependency set, see the offline options in
`prepare_compliance.py --help`. A rebuilt image invalidates release-matched
checksums and relink evidence; regenerate and verify them before publication.

The exporter creates a clean package under `build/publication`. It includes
the firmware source and tests, third-party display material, tools, manuals,
release images, and required compliance materials. It excludes the surrounding
private repository, Git metadata, local build output, backups, live saves,
SD logs, caches, and accelerated-life builds.

## Upload

Review the clean exported folder, then publish its contents to the repository
root. With Git:

```sh
git add .
git diff --cached --stat
git diff --cached --check
git commit -m "Publish Buddy source and firmware release"
git push
```

Alternatively, use GitHub's upload interface and preserve the exported
directory structure. Do not upload the enclosing timestamped folder as a new
top-level directory. Preserve third-party notices and release compliance
materials. The repository's root [LICENSE](../LICENSE) covers Buddy-owned
work; it does not replace dependency terms.

An optional GitHub Release can attach the complete exported ZIP. It is not
required for source access: the repository itself contains the source, and
GitHub's Code → Download ZIP can retrieve the tree.

## Included and excluded

The public tree includes `firmware/Buddy`, tests, `third_party/`, `tools/`,
installer utilities, manuals, artwork previews, current firmware packages,
and corresponding third-party notices/source. It excludes private history,
device backups, live life saves, SD logs, recordings, caches, and temporary
builds. Never copy the full Desktop/Buddy development directory to GitHub.

Buddy-owned materials use the MIT License. Third-party components, including
some Espressif binary components/models, keep their original terms and may
not all be source-available. Review [LICENSE-SCOPE.md](../LICENSE-SCOPE.md),
[NOTICE.md](../NOTICE.md), and [THIRD_PARTY.md](../THIRD_PARTY.md) before
redistributing a combined firmware image or assembled device.
