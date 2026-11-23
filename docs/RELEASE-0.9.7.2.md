# Diablo GBA 0.9.7.2

## Build fixes

- Add the missing `game-source/tools/pack_ui_audio.py`. Since 0.9.7, `scripts/regenerate_foundation.py` runs this stage and `src/audio.c` includes `generated/ui_audio.h`, but the tool was never committed, so complete generation from the public tree stopped at `pack_ui_audio`. The tool converts the original `Sfx\Items\Titlemov.wav` and `Sfx\Items\Titlslct.wav` to 6144 Hz signed 8-bit mono PCM with a 1 KiB DMA guard, the same format as `pack_item_grab_rc7.py`, in `AUDIO_UI_MOVE`, `AUDIO_UI_SELECT` order. The 0.85 gain follows the item-grab cue; it was reconstructed and may differ from the maintainer's private tool.
- Define `THUMB_FN` only for ARM targets in `actor_frames.c`, `audio.c` and `quests.c`. The unconditional `target("thumb")` attribute broke the host-compiled decoders used by `verify_hero_pixels` and `pack_monster_pool.py`. GBA code generation is unchanged: with the 0.9.7.1 sources restored over the same generated assets, the Standard ROM was byte-identical.

## Hero corruption when a monster dies

Freshly generated builds overwrote the hero sprite with corpse pixels whenever a monster died and its death animation started. The 0.9.7 maintainer ROM was not affected.

`death_sprites.inc` decodes animated corpses into two free entries of the living actors' raw-frame cache, but derived that cache from `RAW_DUNGEON_ART`, while `load_town_frame` uses `DUNGEON_LAYOUT_OFFSET`. `pack_dungeon.py` writes `RAW_DUNGEON_ART` as the RAM-limited 80576, but the final `pack_asset_sizes.py` stage rewrites `packed_assets.h` from file sizes and sets it to the full `dungeon_art.bin` size (113728 for the tested MPQ). Corpse decodes then landed at `town_map+126528` and later inside `DEATH_SPRITE_STAGE`, including the hero stage buffer at `+10*2048`.

Both corpse cache addresses now use `DUNGEON_LAYOUT_OFFSET`. The resulting literals (`0x16cc0`, `0x174c0`) match the 0.9.7 maintainer ROM. The reporter confirmed that the glitch no longer occurs.

## Performance

All changes keep the same visible output and simulation:

- Minimap streaming: the 1024-bit dirty-cell scan skips clear bytes and jumps to the next dirty bit, with the same visiting order and 1024-bit budget. Edge lines in `ms_pixels` skip clear bytes and visit only set edge bits. Floor division by 2, 4 and 8 uses arithmetic shifts.
- Floor list (`floor_prepare`): visibility bounds are computed once, remains and drops are scanned in two loops in the previous order, and off-screen drops are culled before the quest/art lookups. The early test allows for the 10-pixel quest-item lift, so it cannot reject an entry the final test would keep.

Measured in mGBA 0.10.2 (libmgba, headless, deterministic input replay). Scenes are loaded from in-game saves, and the minimap is enabled with START, DOWN. The tested MPQ is 517501242 bytes, SHA-256 `6bc120a6…774e7`. Busy cycles exclude the VCOUNT wait in `main`. The baseline is 0.9.7.1 with only the corpse-cache fix.

| Scene | FPS before | FPS after | Busy cycles per rendered frame |
| --- | ---: | ---: | ---: |
| Floor 1, idle (10 s) | 59.73 | 59.73 | −1.7% |
| Floor 1, walking (30 s) | 48.01 | 49.34 | −4.8% |
| Floor 5, idle (10 s) | 54.75 | 54.65 | −5.6% |
| Floor 5, walking with combat (30 s) | 36.57 | 38.39 | −8.9% |
| Floor 13, idle (10 s) | 59.73 | 59.73 | −3.5% |
| Floor 13, walking with combat (30 s) | 42.11 | 42.80 | −4.3% |

At the end of every scene, the hero position, HP, level, kills and the complete minimap VRAM (BG2 map and referenced tiles) match the baseline. Screenshot differences are limited to actor animation phases, which change when more frames are rendered. These are selected scenes, not whole-game averages. FPS moves in VBlank-sized steps, so the busy-cycle column is the more sensitive measure.

## Tests

`tests/test_build_regressions.py` runs from `scripts/test_core.py`. It checks four things:

- Every regeneration tool named by `scripts/regenerate_*.py` exists.
- `actor_frames.c` compiles with the host compiler.
- The corpse cache uses the living actor cache base.
- The rewritten minimap scans, floor division and drop culling match the previous algorithms on random input.

The first three checks fail against the 0.9.7.1 tree.

## Validation and limits

Complete generation from the tested MPQ succeeded with Python 3.13 (3.12 is the documented reference) and Zig 0.13.0. Standard and EZ4 ROMs built with header and static memory-budget checks: IWRAM code 24,476 bytes, static EWRAM 262,084 bytes, both unchanged. No new physical-hardware, link-play or co-op validation is claimed for 0.9.7.2.

Generated `prop_atlas.lz` and `ending_common*.lz` still differ from the 0.9.7 maintainer ROM. For props, all 236 reconstructed frames are pixel-identical; the maintainer atlas only carries 68 additional unreferenced tiles. The ending-video difference was not inspected visually.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
