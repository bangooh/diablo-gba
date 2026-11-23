# Diablo GBA 0.9.7.2

## Fixes

- Add the missing `game-source/tools/pack_ui_audio.py`. `regenerate_foundation.py` has run this stage since 0.9.7, so complete generation from the public tree stopped at `pack_ui_audio`. The tool rebuilds the original Titlemov/Titlslct cues in the same 6144 Hz signed 8-bit format as the item-grab cue. Its 0.85 gain was reconstructed and may differ from the original private tool.
- Define `THUMB_FN` only for ARM targets in `actor_frames.c`, `audio.c` and `quests.c`. The unconditional Thumb attribute stopped the host-compiled decoder used by `verify_hero_pixels`. GBA output is unchanged: with the 0.9.7.1 sources restored over the same generated assets, the Standard ROM was byte-identical.
- Fix hero sprite corruption each time a monster died in freshly generated builds. Animated corpses use two free entries of the living actors' raw-frame cache, but derived its address from `RAW_DUNGEON_ART`. The final `pack_asset_sizes.py` stage rewrites that value to the full `dungeon_art.bin` size, which moved corpse decodes into the hero staging buffer. Corpses now use `DUNGEON_LAYOUT_OFFSET`, like `load_town_frame`, and the resulting addresses match the 0.9.7 release ROM.
- Display V 0.9.7.2 in Standard and EZ4 builds.

## Performance

- Minimap streaming skips clear bytes in its dirty-cell scan and edge lines, with the same visiting order and per-frame budget. Floor division by 2, 4 and 8 uses arithmetic shifts.
- The floor list hoists its visibility bounds, scans remains and drops in separate loops in the previous order, and culls off-screen drops before the quest and art lookups. The early test allows for the 10-pixel quest-item lift.

Measured in mGBA 0.10.2 with the minimap enabled (START, DOWN). Each scene was loaded from the same in-game save with the same input sequence. The baseline is 0.9.7.1 with only the corpse fix. Busy cycles exclude the VCOUNT wait.

| Scene | Previous | Current | Busy cycles per frame |
| --- | ---: | ---: | ---: |
| Floor 1, walking (30 s) | 48.01 FPS | 49.34 FPS | −4.8% |
| Floor 5, walking with combat (30 s) | 36.57 FPS | 38.39 FPS | −8.9% |
| Floor 13, walking with combat (30 s) | 42.11 FPS | 42.80 FPS | −4.3% |
| Floors 1, 5 and 13, idle (10 s) | 59.73 / 54.75 / 59.73 FPS | 59.73 / 54.65 / 59.73 FPS | −1.7% to −5.6% |

These are selected scenes, not whole-game averages. FPS changes in VBlank-sized steps, so small CPU savings do not always appear as higher FPS.

## Validation and limits

Complete generation from a 517,501,242-byte MPQ succeeded with Zig 0.13.0 and Python 3.13 (3.12 remains the reference). Standard and EZ4 ROMs pass header and static memory-budget checks. IWRAM and EWRAM use is unchanged from 0.9.7.1.

At the end of every measured scene, the hero position, HP, level and kills, and the complete minimap VRAM, match the baseline. Screenshots differ only in actor animation phase. The reporter confirmed that the hero no longer breaks when monsters die.

`tests/test_build_regressions.py` now runs from `test_core.py`. It fails on the 0.9.7.1 tree for the missing tool, the host decoder build and the corpse cache address. It also compares the new minimap and floor-list loops with the previous algorithms on random input.

Generated `prop_atlas.lz` and the ending video still differ from the 0.9.7 release ROM. All prop frames are pixel-identical; the release atlas only carries 68 unreferenced tiles. The ending video was not compared visually. No new physical-hardware, link-play or co-op validation is claimed for 0.9.7.2.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
