#!/usr/bin/env python3
"""Bind Stage60 UI/door/minimap changes to a reproducible ROM and source set."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
rom = args.build / 'diablo-gba-assets.gba'
elf = args.build / 'diablo-gba-assets.elf'
build = json.loads((args.build / 'diablo-gba-assets-build-report.json').read_text())
assert sha(rom) == build['sha256']
assert rom.stat().st_size == 33554432
assert (sum(rom.read_bytes()[160:190]) + 0x19) & 255 == 0
assert build['ewram_static_bytes'] == 261924
assert build['iwram_code_bytes'] == 24368

paths = sorted(p for p in (root / 'src').rglob('*') if p.is_file())
paths += [root / 'build.py', root / 'link.ld']
changed = [str(p.relative_to(root)) for p in paths
           if not (args.baseline / p.relative_to(root)).exists()
           or sha(p) != sha(args.baseline / p.relative_to(root))]
assert set(changed) == {'src/gba_mode0.c', 'src/menu.inc',
    'src/town_dialogue.inc', 'src/spells.c', 'src/spells.h', 'build.py'}, changed
# The spell change is an added read-only HUD query; all pre-existing casting
# implementation bytes remain identical to the Stage59 baseline.
spell = (root / 'src/spells.c').read_text()
start = spell.index("/* Mirror request's actual mana")
end = spell.index('static int source_valid', start)
assert spell[:start] + spell[end:] == (args.baseline / 'src/spells.c').read_text()

data = elf.read_bytes()
phoff = struct.unpack_from('<I', data, 28)[0]
phsize, phcount = struct.unpack_from('<HH', data, 42)
payload = 0
for i in range(phcount):
    kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', data, phoff + i * phsize)
    if kind == 1 and filesz and 0x08000000 <= paddr < 0x0a000000:
        payload = max(payload, paddr - 0x08000000 + filesz)
assert payload <= 33554432
asset_paths = ['tools/pack_spell_icons.py', 'tools/pack_spell_expansion.py',
    'tools/pack_door_arches.py', 'source_assets/door_arches/L1S.CEL',
    'source_assets/spell_colors_stage60/control.cpp',
    'source_assets/spells/SpelIcon.CEL', 'source_assets/spells/l1.pal',
    'generated/door-arches-report.json', 'generated/dungeon_art_raw.rle',
    'generated/spell-icon-colors-report.json']
result = {
    'result': 'PASS', 'rom_sha256': sha(rom), 'elf_sha256': sha(elf),
    'baseline_rom_sha256': sha(args.baseline / 'diablo-gba-assets.gba'),
    'changed_production_sources': changed,
    'source_files': {str(p.relative_to(root)): sha(p) for p in paths},
    'asset_sources_and_tools': {name: sha(root / name) for name in asset_paths},
    'generated_headers': {str(p.relative_to(root)): sha(p) for p in sorted((root / 'generated').glob('*.h'))},
    'linked_objects': {p.name: sha(p) for p in sorted(args.build.glob('*.o'))},
    'rom_payload_bytes': payload, 'rom_free_bytes': 33554432 - payload,
    'ewram_static_bytes': build['ewram_static_bytes'],
    'ewram_static_free_bytes': 262144 - build['ewram_static_bytes'],
    'iwram_code_bytes': build['iwram_code_bytes'],
    'iwram_code_headroom_bytes': 24576 - build['iwram_code_bytes'],
    'iwram_stack_reserved_bytes': 8192,
    'pre_existing_casting_implementation_unchanged': True,
    'physical_console_tested': False,
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k:v for k,v in result.items() if k not in (
    'source_files','asset_sources_and_tools','generated_headers','linked_objects')}, indent=2))
