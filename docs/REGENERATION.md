# MPQ asset generation and verification

The complete entry point is `scripts/regenerate_complete.py`; installation and
variant commands are in BUILDING.md. The pipeline uses the supplied MPQ,
project code/constants, pinned upstream reference code and approved font inputs.
It does not read the private release asset directory to generate output.
Comparison against that directory is a separate read-only verification step.

## Verified components

- All 19 checked font headers/blobs match the retained release. The 3x5, 5x5
  and 7x7 fonts are retained; the last uses an 8x8 storage cell.
- Production-C decoder checks cover 648 hero clips / 55,896 frames and 81
  casting clips / 4,536 frames. Source conversion pixels, release pixels and
  decoder output/scratch guards pass.
- All 170 native monster palette files match the release.
- The complete spell expansion header matches, including 227 geometry records
  and both small/large tile arrays. Spellbook palettes also match.
- Town shortcut/well/tile streams match, including the 137 appended tiles.
- All 236 decoded prop frames match. Open door frames retain the existing
  one-transparent-pixel-per-2x2 pattern. Atlas deduplication can change the
  encoded byte stream while preserving every decoded frame.
- The common ending stills and their palettes are intentionally replaced by
  frames extracted from the supplied MPQ. This is not a byte-identical rebuild
  of the older release ROM. The ending-video decoder checks 373 frames.
- The audio master checkpoint verifies 179 intermediate PCM files by exact
  length/hash before final rate conversion. Already resampled runtime audio
  cannot silently become the next conversion's source.

See VALIDATION.md for the public validation summary. Detailed internal generation
and release comparison reports are retained privately and are not distributed here.

## Resume behavior

Every completed extended stage records arguments, source/dependency hashes and
context hashes. A changed completed stage is rejected: start a new output folder
instead of mixing old downstream output with new upstream processing. Original
background, audio, floor and monster-sharing inputs have private checkpoints.
Incomplete stages may be retried against their preserved inputs.

The first integration run required reviewed repairs to intermediate inputs while
restoring the pipeline. All replacement inputs came from MPQ generation or the
same run's original intermediates. This is not an independent second-machine,
unattended clean-build reproducibility test; that verification remains open.

## Tested scope and limits

The input was the Diablo 1.0 MPQ, 517501242 bytes. The GOG MPQ and Windows/macOS
host generation have not been tested here. The build toolchain is Zig 0.13.0;
Python dependency versions are pinned in requirements-assets.txt. GCC/G++ and
ffmpeg are also required. No game data is included in this source package.

The Standard ROM passed mGBA 0.10.3 fresh-character creation, town/dungeon entry,
128KB save export and fresh-instance reload. Existing fixtures loaded floors 5
and 16 with visible scenes. A completed --resume build produced an identical ROM.
EZ4 passed compilation and ROM-header checks; its cartridge save driver was not
validated on hardware.

Emulator boot/save checks and static build checks do not establish physical GBA,
flash-cartridge, link-cable or every quest/difficulty combination compatibility.
Standalone graphical converter packages are a separate packaging and host-test step.
