# Diablo GBA 0.9.7.6

## Changes

- Stop diagonal walking from wobbling. Motion interpolated each axis from its signed Q8 coordinate, so moving toward negative X or Y crossed whole pixels a frame earlier than moving toward positive. On 2:1 screen diagonals the camera and sprites then moved in Y alone between X steps, a visible stair-step. Each axis now floors the distance travelled from the start of the step. Y moves only together with every other X pixel in all four diagonal directions, and a step still ends exactly on the destination cell.
- Show the top-left LEVEL/DIFFICULTY label only while the minimap is on, in the minimap's line colour. Turning the map off with START then DOWN clears it. This applies to both Right and Center layouts. The label is redrawn when the map colour changes with the area. The dark one-pixel outline is kept for legibility.
- Accept GOG's DIABDAT.MPQ (`setup_diablo_1.09_hellfire_v4_(78466).exe`), whose `gendata\logo.smk` is a re-encode of the same logo movie. docs/BUILDING.md lists the size, MD5 and SHA-256 of both tested archives. Both build a byte-identical ROM.
- Display V 0.9.7.6 in Standard and EZ4 builds.

## Validation and limits

- Diagonal motion: in mGBA camera traces, steps where the screen moved in Y without X went from 6/35/6/15 (by direction) to 0 in every direction. Static scenes are pixel-identical to 0.9.7.5.
- Label: on floor 1 the label pixels match the minimap ink exactly, and no label pixel remains with the map off.

Measured scenes, FPS (map on, Right):

| Scene | 0.9.7.5 | 0.9.7.6 |
| --- | ---: | ---: |
| Floor 1 walking | 49.28 | 49.87 |
| Floor 5 walking | 39.09 | 39.32 |
| Floor 13 walking | 43.53 | 43.63 |

IWRAM code is 24,420 bytes and static EWRAM is 262,084 bytes. Asset-free checks pass. `tests/test_build_regressions.py` covers the diagonal motion rule and the GOG logo encoding.

No new physical-hardware or link-cable validation is claimed for 0.9.7.6.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
