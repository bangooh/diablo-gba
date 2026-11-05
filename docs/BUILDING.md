# Building the source candidate

## Complete MPQ generation

The tested environment is Linux x86-64, Python 3.12, GCC/G++, ffmpeg and
Zig **0.13.0**. Install Python dependencies from `requirements-assets.txt`.
Zig 0.14.1 is not a substitute: it exceeded this project's IWRAM budget.

```sh
python -m pip install -r requirements-assets.txt
python scripts/regenerate_complete.py --mpq /private/DIABDAT.MPQ \
  --output /private/new-diablo-build --zig /path/to/zig
```

Run from the repository root. The output directory must be new and outside this
source tree. It will contain extracted game assets and must remain private.
The tested Diablo 1.0 MPQ is 517501242 bytes; the GOG MPQ has not been tested
with this generation pipeline. The process can take tens of minutes.

The Standard ROM and ELF are produced under `rom/`. `complete-generation-report.json`
records the input MPQ and generated-file hashes; stage logs are retained separately.
Runtime checks are separate from a successful compile.

If a stage fails, correct the problem and use the same command with `--resume`.
Completed stages are reused only when their arguments and tracked code match.
If an already completed stage changed, use a new output directory. Later stages
can overwrite earlier outputs, so bypassing that guard is unsafe. Immutable
checkpoints preserve original background, audio, floor and monster inputs.

## Build another variant

For freshly generated assets, build the EZ4 variant in a separate private copy:

```sh
python scripts/build_generated_variant.py --project /private/new-diablo-build/game-source \
  --variant ez4 --zig /path/to/zig --output /private/new-diablo-ez4
```

This installs `variants/flash-ez4.c` and the EZ4 frontend label without changing
the Standard source. It also accepts `--variant standard`.

## Converter source

The converter source is included without config.h, executables or encrypted ROM
payloads. It packages an already-built ROM; it is separate from MPQ asset generation.
Its `build_release.py` accepts --zig, --rom, --mpq, --gog-mpq and --output.
Do not commit generated payloads. Windows/macOS GUI and physical flash-cartridge
checks are separate from Linux builds and emulator checks.

## Asset-free checks

Run the three commands in README.md. The lobby suite covers 30 departure,
rejoin and launch cases; it does not replace physical link-cable testing.
