"""Run the recovered MPQ generation sequence in a private working directory.

Build success and runtime verification are recorded separately.
No release generated-assets directory is used as a generation input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mpq', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--zig', type=Path)
    p.add_argument('--resume', action='store_true')
    args = p.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        p.error('Output must be outside the public source tree')
    if not args.resume:
        subprocess.run([sys.executable, str(ROOT / 'scripts/regenerate_foundation.py'),
                        '--mpq', str(args.mpq.resolve()), '--output', str(output), '--actors'], check=True)
    elif not (output / 'regeneration-report.json').is_file():
        p.error('Resume requires a completed foundation report')
    command = [sys.executable, str(ROOT / 'scripts/regenerate_extended.py'),
               '--mpq', str(args.mpq.resolve()), '--foundation', str(output)]
    if args.zig:
        command += ['--zig', str(args.zig.resolve())]
    subprocess.run(command, check=True)
    files = []
    for path in sorted((output / 'game-source/generated').iterdir()):
        if path.is_file() and path.suffix in ('.bin', '.lz', '.rle', '.h', '.inc'):
            files.append(dict(file=path.name, bytes=path.stat().st_size,
                              sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report = dict(mpq_bytes=args.mpq.stat().st_size,
                  mpq_sha256=hashlib.sha256(args.mpq.read_bytes()).hexdigest(),
                  generation_completed=True, rom_build_completed=bool(args.zig),
                  runtime_verified=False, outputs=files)
    (output / 'complete-generation-report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
