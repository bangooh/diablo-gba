# MPQ asset generation

Use `scripts/regenerate_complete.py` to generate assets from your own
`DIABDAT.MPQ`. See [BUILDING.md](BUILDING.md) for dependencies and commands.
The pipeline uses the supplied MPQ, project constants, pinned upstream reference
code and attributed font inputs. Game assets and ROMs are not bundled.

## Inputs and output

The generation tools extract and convert graphics, animations, palettes, maps,
audio and ending-video frames. Required input maps and integrity manifests are
tracked in `docs/`; they are part of the build and must be retained.
The 3x5, 5x5 and 7x7 font inputs retain their existing attribution; the 7x7 font
uses an 8x8 storage cell.

Generated assets, checkpoints, logs and ROMs belong in a private output directory
outside the repository. Optional comparison tools can check generated output
against a separately supplied reference; that reference is not a generation input.

## Resuming a build

Use the original command with `--resume` after correcting an incomplete stage.
Completed stages record arguments and dependency/context hashes. If a completed
stage changes, start a new output directory rather than mixing outputs from
different runs. Checkpoints preserve original background, audio, floor and
monster inputs before downstream conversion.

See [VALIDATION.md](VALIDATION.md) for tested scope and remaining limitations.
