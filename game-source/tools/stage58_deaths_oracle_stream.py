#!/usr/bin/env python3
"""Current floor mask/cache transitions versus full historical recomposition.

Reuses the established terrain fixture, updating its outdated constants and
prop access adapter locally. The original historical test is never modified.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--candidate-source', type=Path, required=True)
p.add_argument('--output', type=Path, default=ROOT / 'generated/stage58-validation/deaths/oracle/stream')
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
old = ROOT / 'tests/check-terrain-cache.py'
tree = ast.parse(old.read_text())
prefix = []
for node in tree.body:
    if isinstance(node, ast.With):
        break
    prefix.append(node)
namespace = {'__file__': str(old)}
saved_argv = sys.argv
sys.argv = [str(old), '--candidate-source', str(args.candidate_source)]
try:
    exec(compile(ast.Module(body=prefix, type_ignores=[]), str(old), 'exec'), namespace)
finally:
    sys.argv = saved_argv
source, header, body, main = (namespace[k] for k in ['s', 'header', 'body', 'main'])
function = namespace['function']
game_header = (ROOT / 'src/game.h').read_text()
floor_constants = '\n'.join(line for line in (ROOT / 'generated/floor_decals.h').read_text().splitlines()
                            if not line.startswith(('extern', '#pragma')))
monsters = int(re.search(r'#define MONSTER_TYPES (\d+)', game_header)[1])
corpses = int(re.search(r'#define MONSTER_CORPSE_TYPES (\d+)', game_header)[1])
item_base = int(re.search(r'FLOOR_ITEM_BASE=(\d+)', floor_constants)[1])
header = header.replace('#define MONSTER_TYPES 50', f'#define MONSTER_TYPES {monsters}')
header = header.replace('#define MONSTER_CORPSE_TYPES MONSTER_TYPES', f'#define MONSTER_CORPSE_TYPES {corpses}')
header = header.replace('#define FLOOR_ITEM_BASE 8000', floor_constants)
death_type = next(line for line in source.splitlines() if line.startswith('typedef struct {uint8_t count,first,indices'))
header += '\n' + death_type + '\n#define DEATH_SPRITES 1\n'
for name in ['DEATH_SPRITE_STATE', 'DEATH_SPRITE_LUT', 'DEATH_SPRITE_STAGE',
             'LIGHT_WINDOW_TABLES', 'FLOOR_EXTRA_KEYS', 'FLOOR_EXTRA_ENABLED']:
    header += re.search(r'^#define ' + name + r'\b[^\n]*', source, re.M)[0] + '\n'
if '#define FLOOR_CACHE_SLOTS ' in source:
    header += re.search(r'^#define FLOOR_CACHE_SLOTS\b[^\n]*', source, re.M)[0] + '\n'
mask_header = args.candidate_source.parent.parent / 'generated/floor_tile_masks.h'
if 'floor_tile_masks[' in source:
    header += mask_header.read_text().replace('#pragma once', '') + '\n'
header += r'''
static const uint8_t *prop_tile(unsigned frame,unsigned tile){
 (void)frame;static uint32_t out[16];
 for(unsigned y=0;y<8;y++)memcpy((uint8_t*)out+y*8,prop_pixels+((tile/8)*8+y)*64+(tile%8)*8,8);
 return (uint8_t*)out;
}
'''
# The old fixture's body already includes the actual cached-picture helper.
helpers = ''.join(function(source, name) for name in ['floor_extra_prepare', 'floor_cache_key', 'floor_cache_store'])
if '#define FLOOR_CACHE_SLOTS ' in source:
    helpers += function(source, 'floor_cache_resolve')
if re.search(r'^static[^\n]*\bfloor_blit_clear\(', source, re.M):
    helpers += function(source, 'floor_blit_clear')
pin = function(source, 'floor_prepare')
pin = pin[pin.index(' /* Pin visible bodies,'):pin.rfind('}')]
# Existing function extraction includes floor_cached_picture later in body.
helpers = 'static const uint8_t *floor_cached_picture(unsigned frame);\n' + helpers
helpers += 'static void cache_pin(FloorDraw *out,unsigned n){unsigned capacity=floor_extra_prepare();\n' + pin + '}\n'
body = helpers + body
main = main.replace(' floor_coverage();unsigned before=', ' cache_pin(FLOOR_LIST(floor_list),floor_count[floor_list]);floor_coverage();unsigned before=')
main = main.replace('assert(*(uint32_t*)asset_floor_decals>8033)', 'assert(*(uint32_t*)asset_floor_decals>FLOOR_BRIGHT_BASE)')
main = re.sub(r'\b8000\b', 'FLOOR_ITEM_BASE', main)
main = re.sub(r'\b8033\b', 'FLOOR_BRIGHT_BASE', main)
main = main.replace('400+(phase-12)%8', '(MONSTER_CORPSE_TYPES*8)+(phase-12)%8')
main = main.replace('420+phase%7', '(MONSTER_CORPSE_TYPES*8)+20+phase%7')
main = main.replace('i<3?400+i:FLOOR_ITEM_BASE', 'i<3?MONSTER_CORPSE_TYPES*8+i:FLOOR_ITEM_BASE')
main = main.replace('FloorDraw items[6]', 'FloorDraw items[16]')
added = r'''
 /* Actual opaque-tile loss/gain, animated->settled->removed, lifted items,
  * shared/de-duplicated cache frames and repeated metadata ownership changes. */
 for(unsigned l=0;l<3;l++){
  setup(levels[l]);FloorDraw items[16];
  for(unsigned step=0;step<360;step++){
   unsigned count=step%32<4?1:step%32<8?0:step%32<16?4:12;
   for(unsigned i=0;i<count;i++){
    unsigned phase=step%8;
    unsigned f=phase==0?0:phase==1?1:phase==2?MONSTER_CORPSE_TYPES*8:
               phase==3?MONSTER_CORPSE_TYPES*8+1:phase==4?0:
               phase==5?FLOOR_ITEM_BASE:phase==6?FLOOR_BRIGHT_BASE:random_n(FLOOR_ITEM_BASE);
    int x=1176+(int)(i%4)*24,y=328+(int)(i/4)*16;
    if(f>=FLOOR_ITEM_BASE)y-=10;
    items[i]=(FloorDraw){x,y,(uint8_t)((y+64+(f>=FLOOR_ITEM_BASE?10:0))/8),(uint8_t)i,(uint16_t)f};
   }
   DEATH_SPRITE_STATE->count=step%13==0;DEATH_SPRITE_STATE->displayed=DEATH_SPRITE_STATE->pending=0;
   if(DEATH_SPRITE_STATE->count)memset(DEATH_SPRITE_STAGE,0xad,24576);
   replace(items,count);cam_x=1120+(int)(step/48%4);cam_y=296+(int)(step/64%5);
   motion[0].x=(cam_x+120)*256;motion[0].y=(cam_y+84)*256;
   light_moved=0;verify();verify();
  }
 }
'''
assert ' assert(saved_calls<reference_calls/3);' in main
main = main.replace(' assert(saved_calls<reference_calls/3);', added + '\n assert(saved_calls<reference_calls);')
with tempfile.TemporaryDirectory(prefix='host-', dir=args.output) as tmp:
    temp = Path(tmp)
    code = header + (ROOT / 'src/floor_decode.h').read_text() + body + main
    (temp / 'check.c').write_text(code)
    subprocess.run(['gcc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    '-I' + str(ROOT), str(temp / 'check.c'), '-o', str(temp / 'check')], check=True)
    done = subprocess.run([str(temp / 'check'), str(ROOT / 'generated/floor_decals.rle')],
                          check=True, capture_output=True, text=True,
                          env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    print(done.stdout, end='')
report = {'result': 'PASS', 'log': done.stdout.splitlines(), 'sanitizers': ['address', 'undefined'],
          'candidate_source': str(args.candidate_source), 'candidate_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'constants': {'MONSTER_TYPES': monsters, 'MONSTER_CORPSE_TYPES': corpses, 'FLOOR_ITEM_BASE': item_base},
          'scope': 'actual floor mask invalidation/coverage/list merge, cache pinning and stream_dungeon compared with full historical recomposition',
          'limits': ['Geometry, lighting and hardware DMA use deterministic host substitutes.',
                     'Reference compositor uses the same validated floor pixel decoder; independent pixel oracle is a separate required check.']}
(args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
