# Diablo GBA 0.9.7.7

## Changes

- Move the corner minimap diagonally on diagonal steps. The box rounded the camera's X and Y to pixels separately. A 2:1 screen diagonal moves the box one pixel on both axes per cell, but the two roundings crossed on different frames, so the map stepped X then Y like a staircase. While the camera tracks a diagonal step, Y is now rounded once and X is derived from it, anchored at the destination, as the Center map already did.
- Target enemies as far below the hero as above. The shared visibility window used by automatic and ranged targeting, spells and CPU party members reached 8 cells above the hero on screen but only 6 below, so enemies toward 6 o'clock dropped out of reach two cells early. Both directions now reach 8 cells. The hero stands at y=84, so a foot 8 cells below is at y=148, in view between the HUD orbs. The side limit (7 cells) and the line-of-sight walk are unchanged.
- Display V 0.9.7.7 in Standard and EZ4 builds.

## Validation and limits

- Corner minimap: on floor 1, mGBA traces of the box origin over 150 frames in each diagonal direction show X-only and Y-only steps (1/1, 6/6, 3/3, 6/6 before) replaced by steps on both axes in every direction. Straight walking and still scenes take the unchanged path. Tristram uses the same code with different origin constants and was not traced.
- Targeting: the change is to the window bounds only. Picking a distant enemy below the hero was not exercised in mGBA. When an enemy name strip is shown, a target 8 cells below has its feet behind the strip.

Measured scenes, FPS (map on, Right):

| Scene | 0.9.7.6 | 0.9.7.7 |
| --- | ---: | ---: |
| Floor 1 walking | 49.87 | 49.87 |
| Floor 5 walking | 39.32 | 38.99 |
| Floor 13 walking | 43.63 | 43.63 |

IWRAM code is 24,420 bytes and static EWRAM is 262,084 bytes. Asset-free checks pass. `tests/test_build_regressions.py` covers both rules.

No new physical-hardware or link-cable validation is claimed for 0.9.7.7.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
