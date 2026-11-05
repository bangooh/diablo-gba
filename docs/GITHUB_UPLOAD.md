# GitHub upload guide

Upload the contents of this source folder as the repository root. The repository is https://github.com/bangooh/diablo-gba.
Suggested repository name: `diablo-gba`; description: `Unofficial C-based Diablo
fan port for Game Boy Advance. Source-available; bring your own game data.`

## Files and checks

Keep the included LICENSE.md, LICENSES/, NOTICE.md, docs/, source, tools,
.github/workflows/ and the two explicitly allowlisted custom font inputs.
Do not select a replacement license in GitHub's new-repository dialog.
Do not upload MPQ, generated assets, ROMs, saves, converter executables or
embedded/encrypted ROM payloads as repository files or release attachments.

From this folder:

```sh
git init -b main
git add .
python scripts/check_public_tree.py --tracked
python scripts/verify_reference_sources.py
python scripts/test_core.py
git diff --cached --stat
```

The two font `.bin` files are deliberate exceptions in .gitignore. Original
reference bytes are protected from Git line-ending normalization. Do not remove
these settings: a clean checkout must retain the recorded hashes and inputs.

Once the intended account/repository is chosen, commit with your own Git identity
and push to that repository. This document does not contain credentials or a
preset remote. GitHub Actions runs asset-free checks; it does not build a ROM.

## Suggested first-release text

Diablo GBA 0.9.5 source distribution. Includes the custom C runtime, MPQ asset
generation tools, pinned reference material and attributed project font inputs.
Game assets and ROMs are not included. A legally obtained DIABDAT.MPQ is required.
See BUILDING.md and REGENERATION.md for tool versions and tested scope.

Saved integration evidence covers Diablo 1.0 MPQ generation, Standard/EZ4 builds
and Standard mGBA save/reload checks. Independent unattended clean generation,
GOG MPQ generation, physical cartridge/link testing and standalone GUI converter
validation remain open. Source-available under the supplied terms; not an
OSI-approved open-source release.
