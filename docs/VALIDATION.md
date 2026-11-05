# Validation scope

## Asset-free checks

The repository checks public file content, pinned reference hashes, required font
inputs and 30 lobby departure, rejoin and launch regression cases. Run the three
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
