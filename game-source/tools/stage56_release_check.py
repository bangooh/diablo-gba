"""Bind the final Stage56 ROM, memory budget and exact build inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('--verify', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report_path = root / 'generated/stage56-validation/source-provenance.json'
build = json.loads((root / 'build/diablo-gba-assets-build-report.json').read_text())
rom = root / 'build/diablo-gba-assets.gba'
assert sha(rom) == build['sha256']
paths = sorted(p for p in (root / 'src').rglob('*') if p.is_file())
paths += [root / 'build.py', root / 'link.ld']
sources = {str(p.relative_to(root)): sha(p) for p in paths}
changed = sorted(str(p.relative_to(root / 'src')) for p in paths
                 if p.is_relative_to(root / 'src')
                 and (not (args.baseline / p.relative_to(root)).exists()
                      or sha(args.baseline / p.relative_to(root)) != sha(p)))
assert changed == ['actor_frames.c', 'death_sprites.inc', 'gba_mode0.c',
                   'monster_ai.c', 'spells.c'], changed
elf = (root / 'build/diablo-gba-assets.elf').read_bytes()
phoff = struct.unpack_from('<I', elf, 28)[0]
phsize, phcount = struct.unpack_from('<HH', elf, 42)
payload = 0
for i in range(phcount):
    kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', elf, phoff+i*phsize)
    if kind == 1 and filesz and 0x08000000 <= paddr < 0x0a000000:
        payload = max(payload, paddr - 0x08000000 + filesz)
rows = subprocess.check_output(['readelf', '-sW', str(root / 'build/diablo-gba-assets.elf')], text=True)
decoder = next(line.split() for line in rows.splitlines() if line.split() and line.split()[-1] == 'xor_nibbles')
assert 0x03000000 <= int(decoder[1], 16) < 0x03006000
assert build['iwram_code_bytes'] < 24576 and build['ewram_static_bytes'] < 262144
assert (root / 'src/audio.c').read_bytes() == (args.baseline / 'src/audio.c').read_bytes()
objects = {p.name: sha(p) for p in sorted((root / 'build').glob('*.o'))}
result = dict(result='PASS', rom_sha256=sha(rom), baseline_rom_sha256=sha(args.baseline / 'diablo-gba-assets.gba'),
              changed_production_sources=changed, source_files=sources, linked_objects=objects,
              rom_payload_bytes=payload, rom_free_bytes=33554432-payload,
              ewram_static_bytes=build['ewram_static_bytes'], ewram_static_free_bytes=262144-build['ewram_static_bytes'],
              iwram_code_bytes=build['iwram_code_bytes'], iwram_code_headroom_bytes=24576-build['iwram_code_bytes'],
              iwram_stack_reserved_bytes=8192,
              xor_nibbles_address=int(decoder[1], 16), xor_nibbles_bytes=int(decoder[2]),
              audio_source_unchanged=True, audio_object_unchanged=objects['audio.o']==sha(args.baseline/'audio.o'),
              physical_console_tested=False)
if args.verify:
    assert result == json.loads(report_path.read_text()), 'Build or source changed after final validation began'
else:
    report_path.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ['source_files','linked_objects']}, indent=2))
