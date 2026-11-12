# Building Diablo GBA

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

If a foundation stage fails, generation stops immediately and prints the log path
and its last 30 lines. Correct the problem and use a **new output directory**.
Only after the foundation has completed successfully can an extended-stage failure
be retried with the same command and `--resume`.
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

## Asset-free checks

Run the three commands in README.md. The lobby suite covers 34 departure,
rejoin and launch cases; it does not replace physical link-cable testing.

## Windows setup (WSL2)

Native Windows generation is not supported. Some packers compile and load Linux
shared libraries; Zig alone does not replace the native compiler or ffmpeg.
Use WSL2 Ubuntu 24.04 and run all generation commands inside its Linux shell:

```sh
sudo apt update
sudo apt install build-essential ffmpeg python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-assets.txt
python scripts/check_build_environment.py --mpq /mnt/c/Games/DIABDAT.MPQ \
  --zig /home/you/zig-linux-x86_64-0.13.0/zig
python scripts/regenerate_complete.py --mpq /mnt/c/Games/DIABDAT.MPQ \
  --output /home/you/new-diablo-build --zig /home/you/zig-linux-x86_64-0.13.0/zig
```

Install the **Linux** Zig 0.13.0 distribution separately. Replace example paths
with your own; `--zig` points to the executable, not its containing directory.
Use Linux paths (`/mnt/c/...` for Windows files), not `C:/...`.
The tested generation platform is Linux x86-64; WSL itself has not yet been
validated end to end. Native macOS generation is also not supported by this entry point.

## MPQ checksums and diagnostics

The entry point now checks dependencies and prints input size, MD5 and SHA-256
before extracting assets. The final generation report also records both hashes.
**No verified reference MPQ digest is currently published in this repository.**
The 517501242-byte size is a reference size, not proof of matching content.
Printed hashes identify your file; they do not by themselves establish compatibility.
If you already have a trusted expected SHA-256, pass
`--expected-mpq-sha256 HEX_DIGEST` to stop on a mismatch.

You can calculate hashes without installing the generation dependencies:

```powershell
Get-FileHash .\private\DIABDAT.MPQ -Algorithm MD5
Get-FileHash .\private\DIABDAT.MPQ -Algorithm SHA256
```

On Linux, use `md5sum /path/to/DIABDAT.MPQ` and
`sha256sum /path/to/DIABDAT.MPQ`. When reporting a failure, include the first
failing stage's log from the output directory's `logs/` folder, the hashes and
your environment details. Do not upload the MPQ or extracted assets.
