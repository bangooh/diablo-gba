#!/usr/bin/env python3
"""Bind Stage61 source/assets, native checks and memory budget to the delivered ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--freeze', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
build = root / 'build'
rom = build / 'diablo-gba-assets.gba'
elf = build / 'diablo-gba-assets.elf'
meta = json.loads((build / 'diablo-gba-assets-build-report.json').read_text())
rom_sha = sha(rom)
assert meta['sha256'] == rom_sha
assert rom.stat().st_size == 33554432
assert (sum(rom.read_bytes()[160:190]) + 0x19) & 255 == 0
frozen = json.loads(args.freeze.read_text())
for name, digest in frozen.items():
    assert sha(root / name) == digest, f'Input changed after integrated build: {name}'
expected = {'src/controller.c', 'src/dungeon.c', 'src/environment_objects.inc',
    'src/exploration.c', 'src/game.h', 'src/gba_mode0.c', 'src/infopanel.inc',
    'src/lighting.inc', 'src/link.h', 'src/menu.inc', 'src/monster_ai.c',
    'src/monster_path.inc', 'src/objects.c', 'src/progress.c', 'src/prop_archive.h',
    'src/prop_dirty.inc', 'src/spells.c', 'src/spells.h', 'build.py'}
production = sorted(p for p in (root / 'src').rglob('*') if p.is_file()) + [root / 'build.py', root / 'link.ld']
changed = [str(p.relative_to(root)) for p in production
    if not (args.baseline / p.relative_to(root)).exists()
    or sha(p) != sha(args.baseline / p.relative_to(root))]
assert set(changed) == expected, changed
data = elf.read_bytes()
phoff = struct.unpack_from('<I', data, 28)[0]
phsize, phcount = struct.unpack_from('<HH', data, 42)
payload = 0
for i in range(phcount):
    kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', data, phoff + i * phsize)
    if kind == 1 and filesz and 0x08000000 <= paddr < 0x0a000000:
        payload = max(payload, paddr - 0x08000000 + filesz)
assert payload <= 33554432
assert meta['ewram_static_bytes'] == 261924
assert meta['iwram_code_bytes'] == 24368
native = [
    'integrated-ui/report.json', 'firebolt/report.json',
    'environment/native/report.json', 'melee-native/report.json',
    'archive-ui/native/report.json', 'archive-ui/legacy-native/report.json',
    'shrines-native/report.json',
    'spell-colors-native/default/report.json',
    'spell-colors-native/gamma10/report.json',
]
checks = {}
for name in native:
    file = root / 'generated/stage61-validation' / name
    report = json.loads(file.read_text())
    assert report.get('result', report.get('status')) == 'PASS', name
    assert report.get('romSha256', report.get('rom_sha256')) == rom_sha, name
    checks[name] = sha(file)
result = {
    'result': 'PASS', 'rom_sha256': rom_sha, 'elf_sha256': sha(elf),
    'baseline_rom_sha256': sha(args.baseline / 'diablo-gba-assets.gba'),
    'changed_production_sources': sorted(changed),
    'build_inputs_sha256': frozen, 'native_validation_sha256': checks,
    'rom_payload_bytes': payload, 'rom_free_bytes': 33554432 - payload,
    'ewram_static_bytes': meta['ewram_static_bytes'],
    'ewram_static_free_bytes': 262144 - meta['ewram_static_bytes'],
    'iwram_code_bytes': meta['iwram_code_bytes'],
    'iwram_code_headroom_bytes': 24576 - meta['iwram_code_bytes'],
    'iwram_stack_reserved_bytes': 8192,
    'save_version': 127, 'save_generator': 17, 'link_version': 131,
    'physical_console_tested': False,
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items() if k != 'build_inputs_sha256'}, indent=2))
