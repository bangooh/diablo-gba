# Diablo GBA 0.9.7.4

## Fixes

- Stop stippling wall faces in front of thick wall mass. When a dungeon loads, each wall half is set to the fixed 75% pattern if floor lies up to four cells diagonally behind it. Before this release, that scan also counted floor on the far side of a solid block. The near faces of such blocks were therefore always see-through, even where the player could not walk behind them. The scan now stops at the first solid cell, for both the cathedral (`fixed_walls_prepare`) and the catacomb, cave and hell (`fixed_world_wall_cell`) paths. Thin walls directly in front of a passage, and walls along diagonal passages, stay stippled.
- Display V 0.9.7.4 in Standard and EZ4 builds.

## Validation and limits

Wall halves that remain stippled, counted when the level loads:

| Level | 0.9.7.3 | 0.9.7.4 |
| --- | ---: | ---: |
| Floor 1, reporter's save | 1,473 | 963 |
| Floor 5 | 2,766 | 1,144 |
| Floor 13 | 3,707 | 1,656 |

On the reporter's floor 1, every walkable cell can be reached from the entrance, so the affected walls stood in front of real passages on the far side of a block, not sealed pockets. In mGBA screenshots from floors 1, 5 and 13, the hero stays visible in the captured scenes. A monster standing just beyond a thick block can now be partly hidden by the block's near face. The captured scenes do not include that case.

Performance and memory are unchanged: IWRAM code is 24,476 bytes and static EWRAM is 262,084 bytes. The measured scenes run at the same or slightly higher FPS (floor 13 walking: 42.80 to 43.14 FPS), and the final game state matches 0.9.7.3. Asset-free checks pass. `tests/test_build_regressions.py` checks that both wall paths stop at solid mass.

No new physical-hardware or link-cable validation is claimed for 0.9.7.4.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
