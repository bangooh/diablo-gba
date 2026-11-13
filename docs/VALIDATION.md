# Validation scope

## Asset-free checks

The repository checks public file content, pinned reference hashes, required font
inputs and 34 lobby departure, rejoin and launch regression cases.
Save tests exercise all three distributed flash implementations with interruption
budgets for initial and replacement commits, corrupt-newest-bank fallback, range
checks and maximum-size archives. They model faults between operations, not
physical partial-sector erases or EZ4 hardware timing. Floor-validation tests
compile production layout declarations with synthetic item-count boundaries;
codec tests cover round trips, truncated network packets and output guards.
Run the three
commands in [README.md](../README.md); CI runs them on pushes and pull requests.

## Integration coverage

Previous integration checks covered Diablo 1.0 MPQ generation, Standard/EZ4 ROM
compilation and Standard-ROM tests in mGBA 0.10.3: fresh character creation,
town/dungeon entry, 128KB save and reload, and floor 5/16 loads.
These integration tests were not rerun during repository cleanup.

Independent unattended clean generation, GOG MPQ generation and physical
GBA/EZ4/link-cable compatibility remain unverified. Compilation and emulator
checks do not establish hardware or every quest/difficulty combination's
compatibility. Windows/macOS host generation has not been tested.

The save-hardening changes add `-fno-strict-aliasing` and validate archived
coordinates and indices before single-player or cooperative floor application.
They preserve the archive layout/version. Fresh Standard/EZ4 ROM recompilation
and static memory budgets were verified for [0.9.6](RELEASE-0.9.6.md), using
existing generated assets. Hardware save/load behavior remains unverified;
host tests do not establish those results. Rebuild all objects after the compiler
flag change.
