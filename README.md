# Diablo GBA — source distribution (0.9.5)

A C-based GBA-specific engine developed with reference to Devilution and
DevilutionX. Unofficial fan project by Bangoo; not affiliated with Blizzard
Entertainment or Nintendo. Special thanks to DevilutionX and team Mupung.

This source-available package includes the runtime, MPQ asset-generation
scripts, and approved custom font inputs with their existing attribution.
Game artwork, sound, music, videos, ROMs and save fixtures are not included.
Repository: https://github.com/bangooh/diablo-gba

## Build and check

See [build instructions](docs/BUILDING.md) and the
[asset-generation guide](docs/REGENERATION.md). The complete generation entry point
is `scripts/regenerate_complete.py`; a legally obtained `DIABDAT.MPQ` is required.
The historical `game-source/build-recipe.py --graphics` is not the supported
clean-generation entry point.

Asset-free checks require Python 3.12+ and a native C compiler:

```sh
python scripts/check_public_tree.py
python scripts/verify_reference_sources.py
python scripts/test_core.py
```

## License and provenance

The root [Sustainable Use License](LICENSE.md) governs this project's code
contributions; upstream-derived material retains its upstream terms and notices.
See [NOTICE](NOTICE.md) and [third-party scope](docs/THIRD_PARTY.md).
No game-asset rights are granted.
This non-commercial license is not an OSI-approved open-source license.
The checks are not legal clearance or proof of independent authorship for every line.

## Verified scope

Previous integration checks covered a complete Diablo 1.0 MPQ build and
mGBA fresh-hero, dungeon, save and reload checks. This is not a claim of an
independent unattended clean build. GOG MPQ generation and physical GBA/EZ4/link
checks remain unverified. See [validation summary](docs/VALIDATION.md).
