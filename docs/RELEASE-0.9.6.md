# Diablo GBA 0.9.6

## Changes

ROM-resident monster AI, objects, spells, arrows, controller, items, progress,
potions and exploration modules now compile as Thumb/O2 in both build paths.
`game_activate_nearby` also uses Thumb. The hot IWRAM renderer retains ARM/O2.
This reduces instruction-fetch width on the 16-bit cartridge bus without
changing simulation tick rates or multiplayer packet timing.

Small UI helpers stay in ROM to leave room for ARM/Thumb interworking stubs.
The outlined automap rounding helper also stays in ROM so the EZ4 variant fits
the existing 24 KiB IWRAM linker budget. No memory region was enlarged.
Thumb compilation rejects references to the two 64-bit division helpers that
do not safely interwork on ARMv4T with the pinned Zig 0.13 toolchain.

The existing offline CPU-party fix is retained: an isolated console may press
R even when its serial role is a client. The save format, save-hardening changes
and ordinary Standard/EZ4 backends are retained. SuperCard Test2 diagnostic
changes are not part of this version.

## Prior matched performance experiment

These results compare the archived 0.9.5 baseline with the supplied Thumb/O2
patch, using Zig 0.13.0 and mGBA 0.10.5. They are evidence for the optimization,
not a complete benchmark of this final 0.9.6 integration or physical hardware.
Each row uses the same save/input sequence and counts committed display frames
over 1,200 hardware refreshes (approximately 20.09 seconds).

| Scene | Baseline FPS | Thumb/O2 FPS | Change |
| --- | ---: | ---: | ---: |
| Single player, floor 1 | 28.27 | 29.86 | +5.6% |
| Single player, floor 5 | 25.43 | 27.08 | +6.5% |
| Single player, floor 9 | 27.87 | 30.01 | +7.7% |
| Single player, floor 16 | 26.78 | 33.05 | +23.4% |
| CPU party, floor 1 | 26.68 | 31.11 | +16.6% |
| CPU party, floor 5 | 29.22 | 30.76 | +5.3% |

Floor 16 uses an additional 700-refresh warm-up. The archived lobby predates
the offline-client fix, so its CPU-party benchmark set up an offline master
before starting. It does not test actual cable play. The lowest one-second
window in the floor-5 CPU test remained about 6.97 FPS in both builds; higher
average FPS does not eliminate all initial stalls.

## Final integration validation

Fresh Standard and EZ4 builds completed with Zig 0.13.0 using existing private
generated assets. Both ROMs are 32 MiB and pass ELF/header validation.

| Static allocation | Standard | EZ4 | Linker limit |
| --- | ---: | ---: | ---: |
| EWRAM | 261,936 B | 261,936 B | 262,144 B |
| IWRAM image | 24,476 B | 24,572 B | 24,576 B |

EZ4 has only four bytes left within the configured IWRAM image budget. Future
changes must check both variants. These figures do not measure stack usage.

The final Standard ROM also passed a floor-5 smoke test in mGBA 0.10.5:
single-player and CPU-party sessions each ran for 1,200 refreshes with A held,
without returning to a menu, loading screen or hero death. Their committed
frame rates were 27.08 and 29.66 FPS respectively. The lobby reported client
role 1; pressing R started the CPU party without modifying lobby state in RAM.
The main menu visibly displays `V 0.9.6`. This smoke test is separate from the
matched archived A/B experiment above.

Asset-free checks pass: build prerequisites, 34 lobby regression scenarios,
all three flash fault models, floor validation, codecs, pinned reference hashes
and public-tree checks. See [validation scope](VALIDATION.md) for limitations.

Physical GBA performance, EZ4 SRAM timing and real multiplayer cable sessions
remain unverified for 0.9.6. Full clean MPQ regeneration and every quest,
difficulty and save/load combination were not repeated for this update.
ROMs, generated game assets and save fixtures are not distributed here.
