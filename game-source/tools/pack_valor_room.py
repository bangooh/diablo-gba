"""Preserve native Valor terrain and audit its fixed spawn layers.

Optional argument: retail DIABDAT.MPQ. Without it use preserved source files.
The generator receives macro layers. Native monster positions are exported
separately; the report records object layers and remaining presentation work.
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source_assets/worldgen'
if len(sys.argv) > 1:
    import mpyq
    from extract_assets import read_mpq
    archive = mpyq.MPQArchive(sys.argv[1], listfile=False)
    for name in ('Blood1', 'Blood2'):
        (SOURCE / (name + '.DUN')).write_bytes(read_mpq(archive, 'Levels\\L2Data\\' + name + '.DUN'))

tables = {}
report = {'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea', 'files': {}}
for name in ('Blood1', 'Blood2'):
    data = (SOURCE / (name + '.DUN')).read_bytes()
    words = struct.unpack('<' + 'H' * (len(data) // 2), data)
    width, height = words[:2]
    assert (width, height) == (10, 16)
    assert len(words) == 2 + width * height * 17
    assert max(words[2:2 + width * height]) < 256
    tables[name.lower()] = data[:4 + width * height * 2]
    layers = {}
    for number, label in enumerate(('auxiliary_0', 'monsters', 'objects', 'auxiliary_3')):
        start = 2 + width * height + number * width * height * 4
        layers[label] = [[i % (2 * width), i // (2 * width), value]
                         for i, value in enumerate(words[start:start + width * height * 4]) if value]
    report['files'][name] = {'sha256': hashlib.sha256(data).hexdigest(), 'width': width, 'height': height, **layers}

source = (ROOT / 'source_assets/monster_catalog/monstdat.cpp').read_text()
conversion = re.search(r'char MonstConvTbl\[\] = \{\s*#endif(.*?)\};', source, re.S)[1]
types = re.findall(r'\bMT_\w+', conversion)
guards = report['files']['Blood2']['monsters']
assert len(guards) == 12 and all(types[value - 1] == 'MT_HORNED' for _, _, value in guards)
report.update({
    'native_fixed_monsters': {'MT_HORNED': 12},
    'coordinates': 'Port cells relative to twice the set-piece macro origin; native 16-cell border removed.',
    'book': [9, 24], 'pedestal': [9, 16],
    'stones': [[3, 10], [15, 10], [9, 17]], 'reward': [9, 3],
    'restore_rectangles_inclusive': [[0, 3, 2, 7], [6, 3, 10, 7], [0, 0, 10, 16]],
    'gameplay_enabled': True,
    'pending': ['native torch decorations and quest voice/specific pedestal sounds', 'Horned Demon charge AI', 'emulator/hardware validation'],
})
header = '#pragma once\n/* Generated from retail Blood1/Blood2.DUN; macro layers only. */\n'
for name, data in tables.items():
    header += 'static const uint8_t valor_' + name + '[] = {\n'
    header += '\n'.join(','.join(map(str, data[i:i + 24])) + ',' for i in range(0, len(data), 24))
    header += '\n};\n'
(ROOT / 'generated/valor_room.h').write_text(header)
(SOURCE / 'valor-source-report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Valor: two native 10x16 macro layers; 12 Horned Demon source positions audited')

(ROOT / 'generated/valor_guards.h').write_text('#pragma once\nstatic const uint8_t valor_guards[12][2]={'+','.join('{%d,%d}'%(x,y) for x,y,_ in guards)+'};\n')
