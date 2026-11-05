#!/usr/bin/env python3
"""Package current sources, required assets and evidence without old compiled builds."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
current_build = {'diablo-gba-assets.gba', 'diablo-gba-assets.elf', 'diablo-gba-assets-build-report.json'}
source_suffixes = {'.c', '.h', '.inc', '.py', '.mjs', '.js', '.json', '.md', '.txt', '.html', '.s', '.log'}
skip_dirs = {'__pycache__', '.zig-cache', 'zig-cache', 'node_modules', '.git'}

def include(file):
    rel = file.relative_to(root)
    if any(part in skip_dirs for part in rel.parts):
        return False
    if rel.parts[0] == 'build':
        return (len(rel.parts) == 2 and file.name in current_build) or file.suffix in source_suffixes
    if file.suffix in {'.o', '.pyc', '.tmp'}:
        return False
    # Host executables under generated validation directories are disposable;
    # extensionless source assets remain included unless they are actual ELF.
    if not file.suffix:
        with file.open('rb') as handle:
            if handle.read(4) == b'\x7fELF':
                return False
    return True

files = sorted(f for f in root.rglob('*') if f.is_file() and include(f))
pending = output.with_suffix('.pending.zip')
with zipfile.ZipFile(pending, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
    for f in files:
        archive.write(f, 'dungeon-gba/' + str(f.relative_to(root)))
with zipfile.ZipFile(pending) as archive:
    assert archive.testzip() is None
    for name in ['src/spells.c', 'src/environment_objects.inc', 'src/dungeon.c',
                 'src/monster_ai.c', 'build/diablo-gba-assets.gba',
                 'generated/stage61-release-report.json', 'ENVIRONMENT-AND-AI-STAGE61.md']:
        assert archive.read('dungeon-gba/' + name) == (root / name).read_bytes(), name
pending.replace(output)
print(json.dumps({'result': 'PASS', 'file': str(output), 'files': len(files),
    'bytes': output.stat().st_size, 'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}, indent=2))
