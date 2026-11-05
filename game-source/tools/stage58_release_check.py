#!/usr/bin/env python3
"""Bind the Stage58 release to its source, unchanged gameplay and memory limits."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--verify', action='store_true')
args = parser.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
rom = args.build / 'diablo-gba-assets.gba'
elf = args.build / 'diablo-gba-assets.elf'
build = json.loads((args.build / 'diablo-gba-assets-build-report.json').read_text())
assert sha(rom) == build['sha256']
assert rom.stat().st_size == 33554432
header = rom.read_bytes()[:192]
assert (sum(header[160:190]) + 0x19) & 255 == 0
assert build['ewram_static_bytes'] < 262144
assert build['iwram_code_bytes'] < 24576

# Only renderer and build-asset helpers may change in this performance release.
for name in ('game.c', 'controller.c', 'arrows.c', 'progress.c', 'spells.c',
             'items.c', 'shop.c', 'quests.c', 'monster_ai.c', 'objects.c',
             'exploration.c', 'flash.c', 'link.c', 'coop.c', 'coop_wire.c',
             'dungeon.c', 'dungeon_visual.c', 'world.c', 'potions.c', 'audio.c',
             'game.h', 'save.c', 'save.h'):
    before, after = args.baseline / 'src' / name, root / 'src' / name
    if before.exists():
        assert before.read_bytes() == after.read_bytes(), 'Gameplay/audio changed: ' + name

data = elf.read_bytes()
phoff = struct.unpack_from('<I', data, 28)[0]
phsize, phcount = struct.unpack_from('<HH', data, 42)
payload = 0
for i in range(phcount):
    kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', data, phoff + i * phsize)
    if kind == 1 and filesz and 0x08000000 <= paddr < 0x0a000000:
        payload = max(payload, paddr - 0x08000000 + filesz)

source_paths = sorted(p for p in (root / 'src').rglob('*') if p.is_file())
source_paths += [root / 'build.py', root / 'link.ld']
changed = [str(p.relative_to(root)) for p in source_paths
           if not (args.baseline / p.relative_to(root)).exists()
           or sha(p) != sha(args.baseline / p.relative_to(root))]
result = {
    'result': 'PASS', 'rom_sha256': sha(rom), 'elf_sha256': sha(elf),
    'baseline_rom_sha256': sha(args.baseline / 'diablo-gba-assets.gba'),
    'changed_production_sources': changed,
    'source_files': {str(p.relative_to(root)): sha(p) for p in source_paths},
    'asset_tools': {name: sha(root / name) for name in
                    ('tools/pack_floor_masks.py', 'tools/pack_cast_frames.py')},
    'cast_asset_manifest_sha256': sha(root / 'generated/cast-frames-report.json'),
    'generated_headers': {str(p.relative_to(root)): sha(p) for p in sorted((root / 'generated').glob('*.h'))},
    'linked_objects': {p.name: sha(p) for p in sorted(args.build.glob('*.o'))},
    'rom_payload_bytes': payload, 'rom_free_bytes': 33554432 - payload,
    'ewram_static_bytes': build['ewram_static_bytes'],
    'ewram_static_free_bytes': 262144 - build['ewram_static_bytes'],
    'iwram_code_bytes': build['iwram_code_bytes'],
    'iwram_code_headroom_bytes': 24576 - build['iwram_code_bytes'],
    'iwram_stack_reserved_bytes': 8192,
    'gameplay_and_save_source_unchanged': True,
    'audio_source_unchanged': True, 'physical_console_tested': False,
}
if args.verify:
    assert json.loads(args.output.read_text()) == result, 'Source or release changed during validation'
else:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items()
                  if k not in ('source_files', 'generated_headers', 'linked_objects')}, indent=2))
