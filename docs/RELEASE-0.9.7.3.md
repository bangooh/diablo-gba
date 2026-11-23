# Diablo GBA 0.9.7.3

## Fixes

- Stop the CPU rogue from firing arrows in arbitrary directions. It chose enemies within an 8-cell box but pressed A without setting a target. Manual targeting uses the narrower on-screen diamond, so enemies near the box corners left the shot without a target, and the arrow followed the rogue's last facing, often away from the enemy while it retreated. Ranged CPUs now choose enemies with the same visibility as manual targeting, and the rogue sets its chosen enemy as the target before firing, as the sorcerer already did.
- Remove the stutter of CPU party members while walking. CPU input is generated before `game_step` decrements the movement timer, so each CPU step missed the tick on which the previous step ended. CPUs walked every 9 ticks with one idle tick between steps, which showed the standing pose. Human players hold the direction and walk every 8 ticks. CPU input now treats the final tick of the move, attack and hit timers as ready.
- Display V 0.9.7.3 in Standard and EZ4 builds.

## Validation and limits

Measured in mGBA 0.10.2 in an offline CPU party on floor 1, using a measurement-only build with counters and the same input sequence before and after:

| Measurement | 0.9.7.2 | 0.9.7.3 |
| --- | ---: | ---: |
| Rogue shots without a target | 13 of 13 | 0 of 3 |
| Consecutive CPU steps 8 ticks apart | 0 | 94 |
| Consecutive CPU steps 9 ticks apart | 80 | 0 |
| CPU steps after a stop | 127 | 13 |

Single-player scenes are unchanged from 0.9.7.2: FPS, busy cycles, the final game state and the minimap VRAM match. Asset-free checks pass, including the lobby scenarios, and `tests/test_build_regressions.py` now checks both fixes.

CPU behavior is part of the deterministic co-op simulation, so linked consoles must run the same version. No new physical-hardware or link-cable validation is claimed for 0.9.7.3.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
