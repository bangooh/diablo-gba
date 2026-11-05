"""Pack preserved retail set-map transmission rectangles and Vile1 DUN layer."""
from pathlib import Path
import hashlib
import json
import re
import struct

root = Path(__file__).resolve().parents[1]
source = root/'source_assets/quest_remaining/setmaps.cpp'
text = source.read_text()

def rectangles(prefix, groups):
    result = []
    room = 1
    for index, area in enumerate(groups, 1):
        match = re.search(rf'BYTE {prefix}{index}\[\]\s*=\s*\{{([^}}]*)\}}', text)
        values = list(map(int, re.findall(r'\d+', match[1])))
        assert len(values) % 4 == 0
        for i in range(0, len(values), 4):
            result.append([*values[i:i+4], room])
            if not area:
                room += 1
        if area:
            room += 1
    return result

king = rectangles('SkelKingTrans', [True, False, True, False])
bone = rectangles('SkelChamTrans', [False, True, False])
vile_source = root/'source_assets/finale/Vile1.DUN'
data = vile_source.read_bytes()
width, height = struct.unpack_from('<HH', data)
# DRLG_SetMapTrans starts at p+2, then skips (macroCells+1)*2
# and three native micro-cell layers, each containing uint16 values.
offset = 4+26*width*height
values = list(data[offset:offset+8*width*height:2])
assert len(values) == 4*width*height and set(values) <= {0, 1}
bits = bytearray((len(values)+7)//8)
for index, value in enumerate(values):
    bits[index//8] |= value << (index % 8)

def array(name, rows):
    return f'static const uint8_t {name}[][5]={{'+','.join('{'+','.join(map(str, row))+'}' for row in rows)+'};\n'

out = '/* Generated from preserved retail setmaps.cpp and Vile1.DUN. */\n#pragma once\n#include <stdint.h>\n'
out += array('fixed_king_rooms', king) + array('fixed_bone_rooms', bone)
out += f'enum {{ FIXED_VILE_ROOM_WIDTH={width*2},FIXED_VILE_ROOM_HEIGHT={height*2} }};\n'
out += 'static const uint8_t fixed_vile_room_bits[]={'+','.join(map(str, bits))+'};\n'
(root/'generated/fixed_rooms.h').write_text(out)
report = {'king_rectangles': len(king), 'bone_rectangles': len(bone),
          'vile_cells': len(values), 'vile_ids': sorted(set(values)),
          'vile_packed_bytes': len(bits),
          'source_sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, vile_source)}}
(root/'generated/fixed-rooms-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report))
