#!/usr/bin/env python3
"""Actual floor cache/compositor versus independent full-pixel asset oracle.

Only a temporary host executable is built; no production or ROM build is made.
The source argument may point to an unapplied Stage58 renderer candidate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--candidate-source', type=Path, required=True)
parser.add_argument('--output', type=Path, default=ROOT / 'generated/stage58-validation/deaths/oracle')
parser.add_argument('--candidate-masks', type=Path, help='Optional packed little-endian uint32 masks[frames][2]')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = args.candidate_source.read_text()


def function(name):
    match = re.search(r'^static[^\n]*\b' + re.escape(name) + r'\([^;\n]*\)\s*\{', source, re.M)
    assert match, name
    begin, end, depth = match.start(), match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end] + '\n'


def decode_asset(asset):
    count, = struct.unpack_from('<I', asset)
    raw = bytearray(count * 4096)
    for frame in range(count):
        off, = struct.unpack_from('<I', asset, (frame + 1) * 4)
        top, rows, bits = asset[off:off + 3]
        assert off % 4 == 0 and top + rows <= 64 and 1 <= bits <= 8
        pal, body = struct.unpack_from('<II', asset, off + 4)
        mask = (1 << bits) - 1
        for y in range(rows):
            p = body + struct.unpack_from('<H', asset, body + y * 2)[0] - 8
            x = 0
            while asset[p]:
                token = asset[p]
                p += 1
                if token >= 128:
                    x += 256 - token
                elif token > 64:
                    length = token - 64
                    color = asset[pal + (asset[p] & mask)]
                    at = frame * 4096 + (top + y) * 64 + x
                    raw[at:at + length] = bytes([color]) * length
                    p += 1
                    x += length
                else:
                    size = (token * bits + 7) // 8
                    packed = int.from_bytes(asset[p:p + size], 'little')
                    at = frame * 4096 + (top + y) * 64 + x
                    raw[at:at + token] = bytes(asset[pal + ((packed >> (i * bits)) & mask)] for i in range(token))
                    p += size
                    x += token
                assert x <= 64
            assert x == 64
    return count, raw


asset = (ROOT / 'generated/floor_decals.rle').read_bytes()
count, decoded = decode_asset(asset)
asset_report = json.loads((ROOT / 'generated/floor-decals-report.json').read_text())
assert count == asset_report['frames']
assert hashlib.sha256(decoded).hexdigest() == asset_report['decoded_sha256']
tile_masks = bytearray()
for frame in range(count):
    mask = 0
    for ty in range(8):
        for tx in range(8):
            if any(any(decoded[frame * 4096 + (ty * 8 + y) * 64 + tx * 8:
                               frame * 4096 + (ty * 8 + y) * 64 + tx * 8 + 8]) for y in range(8)):
                mask |= 1 << (ty * 8 + tx)
    tile_masks.extend(struct.pack('<Q', mask))
(args.output / 'reference-tile-masks.bin').write_bytes(tile_masks)
if args.candidate_masks:
    assert args.candidate_masks.read_bytes() == tile_masks, 'candidate tile masks differ from independent full-frame occupancy'
constants = '\n'.join(line for line in (ROOT / 'generated/floor_decals.h').read_text().splitlines()
                      if not line.startswith(('extern', '#pragma')))
mask_header = ''
if 'floor_tile_masks[' in source:
    mask_path = args.candidate_source.parent.parent / 'generated/floor_tile_masks.h'
    mask_header = mask_path.read_text()
    rows = re.findall(r'\{([0-9,]+)\}', mask_header)
    packed_masks = bytes(int(v) for row in rows for v in row.split(','))
    item_base = int(re.search(r'FLOOR_ITEM_BASE=(\d+)', constants)[1])
    assert len(rows) == item_base and all(len(row.split(',')) == 8 for row in rows)
    assert packed_masks == tile_masks[:item_base * 8], 'header masks differ from independent full-pixel occupancy'
death_type = next(line for line in source.splitlines() if line.startswith('typedef struct {uint8_t count,first,indices'))
floor_type = next(line for line in source.splitlines() if line.startswith('typedef struct ') and line.endswith(' FloorDraw;'))
macro_names = ['TERRAIN_CACHE', 'DEATH_CACHE', 'DEATH_KEYS', 'FLOOR_COVERAGE', 'FLOOR_ANIMATED',
               'DEATH_SPRITE_STATE', 'DEATH_SPRITE_LUT', 'DEATH_SPRITE_STAGE', 'LIGHT_WINDOW_TABLES',
               'FLOOR_EXTRA_KEYS', 'FLOOR_EXTRA_ENABLED']
if '#define FLOOR_CACHE_SLOTS ' in source:
    macro_names.append('FLOOR_CACHE_SLOTS')
macros = []
for name in macro_names:
    match = re.search(r'^#define ' + name + r'\b[^\n]*', source, re.M)
    assert match, name
    macros.append(match.group())
header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/game.h"
#include "src/world_rooms.h"
#define UI_FN
#define DEATH_SPRITES 1
#define WORLD_TILES 651
Game game;
struct {uint32_t before[8];uint16_t words[384*208];uint32_t after[8];} arena;
#define town_map arena.words
uint8_t *asset_floor_decals,*raw;
uint8_t town_depth[651][64];
uint16_t depth_max[651];
unsigned floor_item_pixels,floor_masked_pixels,floor_count[2],floor_list;
'''
header += death_type + '\n' + floor_type + '\n' + '\n'.join(macros) + '\n' + constants + '\n' + mask_header.replace('#pragma once', '') + '\n'
header += r'''
#ifdef FLOOR_CACHE_SLOTS
#define META_END ((uint16_t*)(FLOOR_CACHE_SLOTS+240))
#else
#define META_END (FLOOR_EXTRA_KEYS+7)
#endif
FloorDraw lists[2][240];
#define FLOOR_LIST(n) lists[n]
static uint32_t seed=0x5637abcdu;
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void *load(const char *name){FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);void *p=malloc(size);assert(p&&fread(p,1,size,f)==(size_t)size);fclose(f);return p;}
'''
body = (ROOT / 'src/floor_decode.h').read_text()
for name in ['floor_extra_prepare', 'floor_cached_picture', 'floor_cache_key', 'floor_cache_store', 'floor_blit']:
    body += function(name)
if '#define FLOOR_CACHE_SLOTS ' in source:
    body += function('floor_cache_resolve')
clear_blit = bool(re.search(r'^static[^\n]*\bfloor_blit_clear\(', source, re.M))
if clear_blit:
    body += function('floor_blit_clear') + '\n#define HAS_CLEAR_BLIT 1\n'
body += function('floor_overlay')
prepare = function('floor_prepare')
assert 'unsigned capacity=floor_extra_prepare();' in prepare
tail = prepare[prepare.index(' /* Pin visible bodies,'):prepare.rfind('}')]
body += 'static void cache_pin(FloorDraw *out,unsigned n){unsigned capacity=floor_extra_prepare();\n' + tail + '}\n'
main = r'''
static unsigned tiles,overlaps,transitions,word_cases;
static void guards(void){
 for(unsigned i=0;i<8;i++)assert(arena.before[i]==0xa5e37429u&&arena.after[i]==0x932cd179u);
 assert(*(uint32_t*)(DEATH_SPRITE_STAGE-4)==0x159cd304u);
 assert(*(uint32_t*)(DEATH_SPRITE_STAGE+24576)==0x620b3f57u);
 assert(LIGHT_WINDOW_TABLES[1][160]==0x38416ed5u&&*META_END==0xc913);
}
static void reset(void){
 memset(town_map,0,sizeof(town_map));memset(&game,0,sizeof(game));memset(town_depth,0,sizeof(town_depth));
 for(unsigned i=0;i<8;i++){arena.before[i]=0xa5e37429u;arena.after[i]=0x932cd179u;}
 *(uint32_t*)(DEATH_SPRITE_STAGE-4)=0x159cd304u;
 *(uint32_t*)(DEATH_SPRITE_STAGE+24576)=0x620b3f57u;
 LIGHT_WINDOW_TABLES[1][160]=0x38416ed5u;*META_END=0xc913;
 for(unsigned i=0;i<4;i++)DEATH_KEYS[i]=65535;
 for(unsigned i=0;i<6;i++)FLOOR_EXTRA_KEYS[i]=65535;
 FLOOR_EXTRA_ENABLED=0;floor_list=0;floor_count[0]=floor_count[1]=0;
}
static void oracle(int px,int py,unsigned slot,uint8_t *pixels){
 for(unsigned i=0;i<floor_count[floor_list];i++){
  const FloorDraw *d=&FLOOR_LIST(floor_list)[i];int dx=px-d->x,dy=py-d->y;
  if(dx<=-8||dx>=64||dy<=-8||dy>=64)continue;
  /* Preserve the actual tile admission for narrowed and raised item art. */
  if(d->frame>=FLOOR_ITEM_BASE&&(dx<=8||dx>=48||dy<48))continue;
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   int sx=dx+(int)x,sy=dy+(int)y;if(sx<0||sx>=64||sy<0||sy>=64)continue;
   unsigned p=y*8+x,c=raw[d->frame*4096+sy*64+sx],z=town_depth[slot][p];
   if(c&&(!z||py+(int)z<=d->foot8*8))pixels[p]=(uint8_t)c;
  }
 }
}
static void compare(int x,int y,unsigned slot){
#ifdef FLOOR_CACHE_SLOTS
 floor_cache_resolve(FLOOR_LIST(floor_list),floor_count[floor_list],FLOOR_EXTRA_ENABLED?10:4);
#endif
 struct{uint32_t left;uint8_t pixels[64];uint32_t right;} actual,expected;
 actual.left=expected.left=0x14cb4937;actual.right=expected.right=0xad267104;
 depth_max[slot]=0;
 for(unsigned p=0;p<64;p++){
  actual.pixels[p]=expected.pixels[p]=(uint8_t)(p*3+7);
  unsigned z=(tiles%4==0||p%3==0)?0:(tiles%4==1)?1:(uint8_t)((p*17)%97+1);
  town_depth[slot][p]=(uint8_t)z;if(z&&y+(int)z>depth_max[slot])depth_max[slot]=(uint16_t)(y+z);
 }
 oracle(x,y,slot,expected.pixels);floor_overlay(x,y,slot,actual.pixels);
 if(memcmp(&actual,&expected,sizeof(actual))){fprintf(stderr,"pixel mismatch tile %u pos %d,%d list %u\n",tiles,x,y,floor_count[floor_list]);abort();}
 tiles++;guards();
}
static void clear_keys(void){for(unsigned i=0;i<4;i++)DEATH_KEYS[i]=65535;for(unsigned i=0;i<6;i++)FLOOR_EXTRA_KEYS[i]=65535;}
static void cache_transitions(unsigned frames){
 reset();assert(floor_extra_prepare()==10);assert(FLOOR_EXTRA_ENABLED);
 for(unsigned i=0;i<10;i++){unsigned f=37+i;floor_cache_store(i,f);const uint8_t *p=floor_cached_picture(f);assert(p&&!memcmp(p,raw+f*4096,4096));}
 for(unsigned reason=0;reason<3;reason++){
  DeathSprites *s=DEATH_SPRITE_STATE;s->count=reason==0;s->pending=reason==1?2:0;s->displayed=reason==2?2:0;
  assert(floor_extra_prepare()==4);assert(!FLOOR_EXTRA_ENABLED);
  /* Simulate actor/OBJ ownership changing every pixel while borrowing is off. */
  memset(DEATH_SPRITE_STAGE,0xd3,24576);
  for(unsigned i=4;i<10;i++)assert(!floor_cached_picture(37+i));
  s->count=s->pending=s->displayed=0;assert(floor_extra_prepare()==10);
  for(unsigned i=0;i<6;i++)assert(FLOOR_EXTRA_KEYS[i]==65535);
  for(unsigned i=4;i<10;i++)assert(!floor_cached_picture(37+i));
  for(unsigned i=4;i<10;i++){unsigned f=37+i;floor_cache_store(i,f);assert(!memcmp(floor_cached_picture(f),raw+f*4096,4096));}
  transitions++;guards();
 }
 /* Original cache survives temporary OBJ ownership; extension never does. */
 for(unsigned i=0;i<4;i++)assert(!memcmp(floor_cached_picture(37+i),raw+(37+i)*4096,4096));
 (void)frames;
}
static void pin_selection(unsigned frames){
 reset();
 for(unsigned trial=0;trial<1200;trial++){
  unsigned wanted[10],wanted_n=0,count=trial%241,capacity=trial%3?10:4;
  DeathSprites *s=DEATH_SPRITE_STATE;s->count=capacity==4;s->pending=s->displayed=0;
  if(capacity==4)memset(DEATH_SPRITE_STAGE,0xc5,24576);
  for(unsigned i=0;i<count;i++){
   unsigned f=rng()%frames;
   if(i%4==0)f=rng()%(MONSTER_CORPSE_TYPES*8);
   if(i&&i%7==0)f=lists[0][i-1].frame;
   lists[0][i]=(FloorDraw){0,0,8,(uint8_t)i,(uint16_t)f};
  }
  for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<count&&wanted_n<capacity;i++){
   unsigned f=lists[0][i].frame,animated=f>=MONSTER_CORPSE_TYPES*8&&f<FLOOR_ITEM_BASE;
   if((pass==0&&!animated)||(pass==1&&f>=MONSTER_CORPSE_TYPES*8))continue;
   unsigned exists=0;for(unsigned j=0;j<wanted_n;j++)if(wanted[j]==f)exists=1;
   if(!exists)wanted[wanted_n++]=f;
  }
  cache_pin(lists[0],count);
#ifdef FLOOR_CACHE_SLOTS
  for(unsigned i=0;i<count;i++){
   unsigned expected=255;
   if(lists[0][i].frame<FLOOR_ITEM_BASE)for(unsigned j=0;j<capacity;j++)if(floor_cache_key(j)==lists[0][i].frame){expected=j;break;}
   assert(FLOOR_CACHE_SLOTS[i]==expected);
  }
#endif
  for(unsigned i=0;i<wanted_n;i++){
   const uint8_t *p=floor_cached_picture(wanted[i]);assert(p&&!memcmp(p,raw+wanted[i]*4096,4096));
  }
  if(capacity==4)for(unsigned i=0;i<24576;i++)assert(DEATH_SPRITE_STAGE[i]==0xc5);
  guards();
 }
}
static void alpha_words(void){
#ifdef HAS_CLEAR_BLIT
 uint32_t pixels[1024]={0};uint8_t *src=(uint8_t*)pixels;
 for(unsigned test=0;test<400000;test++){
  unsigned shift=(test/65536)%3*8,a=test<196608?(test&65535)<<shift:rng(),b=rng();
  pixels[0]=a;pixels[1]=b;
  struct{uint32_t before,words[2],after;} got,wanted;
  got.before=wanted.before=0x8e645324;got.after=wanted.after=0x428cea3;
  got.words[0]=wanted.words[0]=rng();got.words[1]=wanted.words[1]=rng();
  for(unsigned i=0;i<8;i++)if(src[i])((uint8_t*)wanted.words)[i]=src[i];
  floor_blit_clear(src,0,0,0,1,(uint8_t*)got.words);
  assert(!memcmp(&got,&wanted,sizeof(got)));word_cases++;
 }
#endif
}
int main(int argc,char **argv){
 assert(argc==3);asset_floor_decals=load(argv[1]);raw=load(argv[2]);unsigned frames=*(uint32_t*)asset_floor_decals;
 cache_transitions(frames);pin_selection(frames);alpha_words();reset();assert(floor_extra_prepare()==10);
 for(unsigned f=0;f<frames;f++){
  floor_count[0]=1;lists[0][0]=(FloorDraw){160,200,33,0,(uint16_t)f};
  clear_keys();for(int y=-8;y<=64;y+=8)for(int x=-8;x<=64;x+=8)compare(160+x,200+y,7);
  floor_cache_store(f%10,f);assert(!memcmp(floor_cached_picture(f),raw+f*4096,4096));
  for(int y=-8;y<=64;y+=8)for(int x=-8;x<=64;x+=8)compare(160+x,200+y,7);
 }
 for(unsigned trial=0;trial<4000;trial++){
  clear_keys();unsigned count=1+rng()%24;floor_count[0]=count;
  int bx=(int)(rng()%160)-80,by=(int)(rng()%160)-80;
  for(unsigned i=0;i<count;i++){
   unsigned f=rng()%frames;if(i%5==0)f=FLOOR_ITEM_BASE+rng()%(frames-FLOOR_ITEM_BASE);
   int x=bx+(int)(rng()%97)-48,y=by+(int)(rng()%97)-48;
   lists[0][i]=(FloorDraw){x,y,(uint8_t)(rng()%64),(uint8_t)i,(uint16_t)f};
   if(i<10)floor_cache_store(i,f);
  }
  /* Different origins include all clipping residues and lifted item offsets. */
  for(int y=-8;y<=64;y+=8)for(int x=-8;x<=64;x+=8)compare(bx+x,by+y,3);
  overlaps++;
 }
 printf("PASS frames=%u tile_comparisons=%u mixed_overlap_scenes=%u borrow_transitions=%u pin_selections=1200 alpha_word_cases=%u\n",frames,tiles,overlaps,transitions,word_cases);
 free(raw);free(asset_floor_decals);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='host-', dir=args.output) as tmp:
    temp = Path(tmp)
    (temp / 'reference.raw').write_bytes(decoded)
    check = temp / 'check.c'
    check.write_text(header + body + main)
    subprocess.run(['gcc', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I' + str(ROOT),
                    str(check), '-o', str(temp / 'check')], check=True)
    done = subprocess.run([str(temp / 'check'), str(ROOT / 'generated/floor_decals.rle'), str(temp / 'reference.raw')],
                          check=True, capture_output=True, text=True,
                          env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    print(done.stdout, end='')
report = {'result': 'PASS', 'scope': 'actual candidate cache helpers and compositor; independent all-frame pixel decode',
          'sanitizers': ['address', 'undefined'], 'asset_frames': count, 'log': done.stdout.splitlines(),
          'candidate_source': str(args.candidate_source), 'candidate_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'asset_sha256': hashlib.sha256(asset).hexdigest(), 'decoded_sha256': hashlib.sha256(decoded).hexdigest(),
          'independent_tile_masks_sha256': hashlib.sha256(tile_masks).hexdigest(),
          'candidate_tile_masks': str(args.candidate_masks) if args.candidate_masks else None,
          'candidate_mask_header_checked': bool(mask_header),
          'limits': ['Does not model GBA DMA or interrupt timing.',
                     'floor_item_pixels and floor_masked_pixels are diagnostic counters with pre-existing cached/uncached differences; pixel output is the contract.']}
(args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
