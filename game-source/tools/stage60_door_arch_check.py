#!/usr/bin/env python3
"""Original MIN/CEL pixel and production door-state selection checks."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

from PIL import Image


def array(text, name):
    return [int(x) for x in re.search(name + r'\[\]=\{([^}]*)\}', text)[1].split(',')]


def original_cel(data, frame, canvas_height=160):
    start, end = struct.unpack_from('<II', data, frame * 4)
    frame_data = data[start:end]
    position = struct.unpack_from('<H', frame_data)[0]
    rows, row = [], []
    while position < len(frame_data):
        code = frame_data[position]
        position += 1
        if code & 128:
            run = [0] * (256 - code)
        else:
            run = list(frame_data[position:position + code])
            position += code
        for value in run:
            row.append(value)
            if len(row) == 64:
                rows.append(bytes(row))
                row = []
    assert not row
    rows.reverse()
    return Image.frombytes('P', (64, canvas_height),
                           bytes(64 * (canvas_height - len(rows))) + b''.join(rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Extract only the original tile/piece decoders; MPQ libraries are not needed.
    module = ast.parse((root / 'tools/extract_assets.py').read_text())
    functions = [node for node in module.body if isinstance(node, ast.FunctionDef)
                 and node.name in ('tile_frame', 'pieces')]
    scope = {'Image': Image, 'struct': struct}
    exec(compile(ast.Module(body=functions, type_ignores=[]), '<native tile decoder>', 'exec'), scope)
    source = root / 'source_assets'
    native_pieces = scope['pieces']((source / 'butcher/L1.CEL').read_bytes(),
                                   (source / 'butcher/L1.MIN').read_bytes())
    special = (source / 'door_arches/L1S.CEL').read_bytes()
    doors = (source / 'butcher/L1Doors.CEL').read_bytes()
    palette = (source / 'butcher/L1_1.PAL').read_bytes()
    rgb = [tuple(palette[i:i + 3]) for i in range(0, 768, 3)]
    mapping = (root / 'generated/background-palette-review/static_index_map.bin').read_bytes()
    dark = [0] + [min(range(1, 256), key=lambda n: sum(
        (rgb[n][k] - rgb[i][k] / 2) ** 2 for k in range(3))) for i in range(1, 256)]
    lut = [mapping[n] for n in dark]
    # Stage61 appends native LAMPS pieces without moving any of the226 prior
    # variants. Their previously-unused colors share the static BG palette.
    old_palette=struct.unpack('<256H',(source/'bg_stage41/bg_palette.bin').read_bytes())
    current_palette=struct.unpack('<256H',(root/'generated/bg_palette.bin').read_bytes())
    static=(root/'generated/bg_static_slots.bin').read_bytes()
    slots=[i for i in range(1,256) if static[i//8]&(1<<(i%8))]
    components=lambda c:(c&31,(c>>5)&31,(c>>10)&31)
    extension_lut=[0]+[mapping[n] or min(slots,key=lambda j:sum((a-b)**2 for a,b in zip(components(old_palette[n]),components(current_palette[j])))) for n in dark[1:]]
    data = (root / 'generated/dungeon_art_raw.rle').read_bytes()
    header = (root / 'generated/dungeon_art.h').read_text()
    ids, flags, sources = [array(header, n) for n in ('dart_tile_ids', 'dart_tile_flags', 'dart_source_piece')]
    manifest = json.loads((root / 'generated/dungeon-art-manifest.json').read_text())
    before = (source / 'bg_stage41/dungeon_art_raw.rle').read_bytes().translate(mapping)
    oldids = manifest['tile_ids']
    report = {'status': 'RUNNING', 'pixel_cases': [], 'native_special_sha256': hashlib.sha256(special).hexdigest(),
              'atlas_sha256': hashlib.sha256(data).hexdigest(), 'source_compositor': 'independent original MIN plus L1S special overlay before resizing',
              'unchanged_pieces': 0, 'appended_native_pieces': []}
    for index, piece in enumerate(sources):
        actual = bytearray(32 * 80)
        for ty in range(10):
            for tx in range(4):
                at = index * 40 + ty * 4 + tx
                tile = data[ids[at] * 64:(ids[at] + 1) * 64]
                assert len(tile) == 64
                assert flags[at] == (0 if not any(tile) else 1 if all(tile) else 2)
                for y in range(8):
                    actual[(ty * 8 + y) * 32 + tx * 8:(ty * 8 + y) * 32 + tx * 8 + 8] = tile[y * 8:y * 8 + 8]
        frame = {393: 7, 395: 8, 408: 7}.get(piece)
        if frame:
            arch = original_cel(special, frame)
            opened = original_cel(doors, 3 if frame == 7 else 4)
            assert not any(a and b for a, b in zip(arch.tobytes(), opened.tobytes())), 'pre/post prop compositing equivalence'
            expected = native_pieces[piece - 1].copy()
            expected.paste(arch, (0, 0), Image.frombytes('L', arch.size, bytes(255 if x else 0 for x in arch.tobytes())))
            expected = expected.resize((32, 80), Image.Resampling.NEAREST).point(lut)
            assert bytes(actual) == expected.tobytes(), 'original source pixel mismatch ' + str(piece)
            report['pixel_cases'].append({'piece': piece, 'arch_frame': frame, 'pixels_compared': 2560,
                                          'open_door_arch_overlap_pixels': 0})
        elif index>=len(manifest['pieces']):
            expected=native_pieces[piece-1].resize((32,80),Image.Resampling.NEAREST).point(extension_lut)
            assert bytes(actual)==expected.tobytes(),'new native LAMPS piece mismatch '+str(piece)
            report['appended_native_pieces'].append({'piece':piece,'pixels_compared':2560})
        else:
            for offset in range(40):
                at = index * 40 + offset
                assert ids[at] == oldids[at]
                assert data[ids[at] * 64:(ids[at] + 1) * 64] == before[oldids[at] * 64:(oldids[at] + 1) * 64]
            report['unchanged_pieces'] += 1
    check = r'''#define GRAPHICS_ASSETS
#include <assert.h>
#include <stdio.h>
#include "src/game.h"
#include "generated/dungeon_art.h"
#include "src/dungeon_doors.h"
static Game g;
static uint8_t lookup[MAP_W*MAP_H];
static unsigned index_of(unsigned p){for(unsigned i=0;i<DART_COUNT;i++)if(dart_source_piece[i]==p)return i;assert(0);return 0;}
int main(void){
 const unsigned closed[]={44,51,214,46,56},opened[]={393,393,408,395,395};
 for(unsigned n=0;n<5;n++){
  unsigned kind=n<3?PROP_DOOR_L:PROP_DOOR_R,v=index_of(closed[n]);
  g.props[0]=(Prop){30,30,kind,0};lookup[30*MAP_W+30]=1;
  assert(dungeon_door_variant(&g,lookup,v,30,30)==v);
  g.props[0].state=1;unsigned after=dungeon_door_variant(&g,lookup,v,30,30);
  assert(dart_source_piece[after]==opened[n]);assert(dart_occludes[after]);
  g.props[0].state=0;assert(dungeon_door_variant(&g,lookup,v,30,30)==v);
 }
 const unsigned from[]={43,45,50,54,55,61,67,68,69,70,72,212,354,355};
 const unsigned to[]={392,394,411,397,398,399,400,401,403,404,406,407,409,410};
 for(unsigned kind=PROP_DOOR_L;kind<=PROP_DOOR_R;kind++){
  g.props[0]=(Prop){30,30,kind,1};
  for(unsigned n=0;n<14;n++){
   unsigned v=index_of(from[n]);int x=30-(kind==PROP_DOOR_R),y=30-(kind==PROP_DOOR_L);
   unsigned after=dungeon_door_variant(&g,lookup,v,x,y);
   assert(dart_source_piece[after]==(from[n]==50&&kind==PROP_DOOR_R?412:to[n]));
  }
 }
 puts("PASS 5 native closed/open/closed cases; 28 original neighboring seam cases");
}
'''
    (out / 'check.c').write_text(check)
    executable = out / 'check'
    subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    '-I' + str(root), str(out / 'check.c'), '-o', str(executable)], check=True)
    result = subprocess.run([str(executable)], text=True, capture_output=True, check=True,
                            env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    report['production_state_check'] = result.stdout.strip()
    report['sanitizers'] = ['address', 'undefined']
    report['status'] = 'PASS'
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
