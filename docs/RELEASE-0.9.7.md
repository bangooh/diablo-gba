# Diablo GBA 0.9.7

## Changes since 0.9.6

- Add title navigation/confirmation and START quick-menu sounds; B closes the R spellbook directly. Retain Gharbad's greeting.
- Skip unchanged town tile scans and reuse the visible actor list for palette planning and dormant monster drawing.
- Skip unchanged dormant synchronization in single player; co-op keeps full synchronization.
- Move packed music decoding into IWRAM and selected hot ROM functions to Thumb.
- Commit ready frames during the current VBlank when the upload deadline allows it.
- Mask and light dungeon actors directly in their staging buffers, eliminating one 2 KiB copy on that path.
- Interpolate co-op motion per elapsed video frame, independently of simulation ticks.
- Restore co-op background music and its Music option. Give the four-player item-effect cache a dedicated 720-byte IWRAM allocation.
- Display V 0.9.7 on the main screen. Standard and EZ4 build labels are updated.

## Validation and limits

Core host tests pass (lobby, flash interruption/fallback, floor archive validation, codecs).
Independent mGBA measurements for patches 0006–0007 versus patches 0001–0005:
floor-5 movement 35.39 -> 44.10 FPS; floor-13 movement 42.95 -> 51.07 FPS.
These are selected scenes, not whole-game averages or comparisons against 0.9.6.

CPU party floor-5 comparison, patches 0001–0007 versus 0001–0010:

| Scene | Previous | Current, BGM on |
| --- | ---: | ---: |
| Movement (20.09 seconds) | 18.62 FPS | 18.57 FPS |
| Combat until death (4.74 seconds) | 16.04 FPS | 16.04 FPS |

CPU equipment randomness was fixed only in benchmark builds; shipping builds retain the original seed behavior. Initial and final Game and four-player data match for these scenarios. Intermediate snapshots were not instruction-aligned and do not prove full determinism. Simulation remained approximately 20 ticks/s. Music Off mutes output but still decodes audio, so it does not substantially reduce processing cost.

Patch-0007 check builds recorded zero out-of-VBlank commit endings and zero mismatches across 1,792 staged actor comparisons. This does not replace timing validation of the final build on hardware.

The patches 0001–0010 build uses 24,416/24,576 bytes of IWRAM for code plus cache and 262,084/262,144 bytes of static EWRAM. The project maintainer reports completed physical-hardware and link-play validation. The automated checks described above were performed separately in mGBA and host tests. Final peak stack usage and exhaustive long-session coverage were not measured in this automated review. This release does not claim a new fix for SuperCard persistence or DS emulator save errors.

No game assets, generated assets, ROMs, MPQ files or save fixtures are included in the public repository. Build assets locally from your own supported DIABDAT.MPQ.

## Death-screen belt correction

Move the retained potion belt to Y=152 when the death menu hides the information panel. Keep both shadow OAM and displayed OAM synchronized, and keep the belt at the bottom during subsequent dead-player redraws. Living-player positioning still follows information-panel visibility.

Targeted mGBA checks reproduced the old Y=136 position and verified Y=152 in the death menu, death options and subsequent HUD redraws, plus normal positioning after revival. The Standard ROM build passed header and memory-budget checks. This correction has not yet been separately verified on physical hardware.
