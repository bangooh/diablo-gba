#!/usr/bin/env python3
"""Check actual C prop tile lookup/blending against the old strided pixels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from stage54_prop_dirty_check import frames, macro

ROOT = Path(__file__).resolve().parents[1]


def braced(source, start):
    """Extract the current small C helper or block, keeping its body verbatim."""
    opening = source.index('{', start)
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError('unclosed production helper')


HOST = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated/prop_atlas.h"
#include "generated/prop_highlight.h"
typedef struct {uint32_t before;uint8_t bytes[64];uint32_t after;} Guarded;
_Static_assert(sizeof(Guarded)==72,"guarded DMA word alignment");
static uint8_t native[PROP_ATLAS_FRAMES][4096];
static uint32_t rng=0xe8a24c31u;
static uint32_t random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
'''

WRAPPER = r'''
static void actual_draw(unsigned frame,int prop_y,int dx,unsigned highlight,
                        uint8_t *pixels,uint8_t *depth,unsigned z){
 unsigned prop=1,slot=0;
 uint8_t prop_draw_frames[1]={(uint8_t)frame};
 int highlighted_prop=highlight?0:-1,foot=(int)z,py=0;
 uint8_t (*town_depth)[64]=(uint8_t (*)[64])depth;
'''

CHECK = r'''
static void native_draw(unsigned frame,int prop_y,int dx,unsigned highlight,
                        uint8_t *pixels,uint8_t *depth,unsigned z){
 /* Original 64px-stride implementation, independent of tile IDs/atlas. */
 const uint8_t *picture=native[frame]+prop_y*64+dx+16;
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  unsigned value=picture[y*64+x];
  if(value){pixels[y*8+x]=highlight?prop_highlight[value]:value;depth[y*8+x]=(uint8_t)z;}
 }
}
static void guard_ok(const Guarded *value){
 assert(value->before==0xa55ab33bu&&value->after==0xc17e9449u);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *input=fopen(argv[1],"rb");assert(input);
 assert(fread(native,1,sizeof(native),input)==sizeof(native));assert(fgetc(input)==EOF);
 assert(!fclose(input));
 unsigned tiles=0,empty=0,solid=0,mixed=0,cases=0,highlight_cases=0;
 uint64_t compared=0;
 static const unsigned depths[]={0,1,7,8,63,64,127,255};
 for(unsigned frame=0;frame<PROP_ATLAS_FRAMES;frame++)for(int row=0;row<8;row++)for(int col=0;col<8;col++){
  int prop_y=row*8,dx=col*8-16;
  assert(prop_y>=0&&prop_y<64&&dx>=-16&&dx<48&&!(prop_y%8)&&!((dx+16)%8));
  const uint8_t *tile=prop_tile(frame,(unsigned)row*8+(unsigned)col);
  unsigned nonzero=0;
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   unsigned value=native[frame][(prop_y+(int)y)*64+dx+16+(int)x];
   nonzero+=!!value;if(tile)assert(tile[y*8+x]==value);else assert(!value);
  }
  assert((tile!=0)==(nonzero!=0));
  if(tile){
   uintptr_t address=(uintptr_t)tile,base=(uintptr_t)asset_prop_atlas;
   assert(address>=base&&address+64<=base+PROP_ATLAS_BYTES&&!(address&3));
  }
  tiles++;empty+=!nonzero;solid+=nonzero==64;mixed+=nonzero&&nonzero!=64;
  for(unsigned trial=0;trial<12;trial++)for(unsigned highlight=0;highlight<2;highlight++){
   Guarded actual_pixels={.before=0xa55ab33bu,.after=0xc17e9449u};
   Guarded actual_depth={.before=0xa55ab33bu,.after=0xc17e9449u};
   uint8_t expected_pixels[64],expected_depth[64];
   for(unsigned i=0;i<64;i++){
    actual_pixels.bytes[i]=trial==0?0:trial==1?255:trial==2?(i&1?1:0):(uint8_t)random_word();
    actual_depth.bytes[i]=trial==0?255:trial==1?0:(uint8_t)random_word();
   }
   memcpy(expected_pixels,actual_pixels.bytes,64);memcpy(expected_depth,actual_depth.bytes,64);
   unsigned z=trial<8?depths[trial]:random_word()&255;
   native_draw(frame,prop_y,dx,highlight,expected_pixels,expected_depth,z);
   actual_draw(frame,prop_y,dx,highlight,actual_pixels.bytes,actual_depth.bytes,z);
   if(memcmp(expected_pixels,actual_pixels.bytes,64)||memcmp(expected_depth,actual_depth.bytes,64)){
    fprintf(stderr,"mismatch frame=%u tile=(%d,%d) trial=%u highlight=%u z=%u\n",frame,col,row,trial,highlight,z);
    return 1;
   }
   guard_ok(&actual_pixels);guard_ok(&actual_depth);
   compared+=128;cases++;highlight_cases+=highlight;
  }
 }
 const unsigned invalid_frames[]={PROP_ATLAS_FRAMES,255,UINT32_MAX};
 const unsigned invalid_tiles[]={64,255,UINT32_MAX};
 unsigned invalid=0;
 for(unsigned i=0;i<3;i++)for(unsigned tile=0;tile<64;tile++){assert(!prop_tile(invalid_frames[i],tile));invalid++;}
 for(unsigned frame=0;frame<PROP_ATLAS_FRAMES;frame++)for(unsigned i=0;i<3;i++){assert(!prop_tile(frame,invalid_tiles[i]));invalid++;}
 printf("{\"status\":\"PASS\",\"frames\":%u,\"tiles\":%u,\"empty_tiles\":%u,"
        "\"opaque_tiles\":%u,\"mixed_tiles\":%u,\"compositor_cases\":%u,"
        "\"highlight_cases\":%u,\"color_depth_bytes_compared\":%llu,\"invalid_lookups\":%u}\n",
        PROP_ATLAS_FRAMES,tiles,empty,solid,mixed,cases,highlight_cases,
        (unsigned long long)compared,invalid);
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'work/stage54/prop-atlas-host')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    generated = ROOT / 'generated'
    sources = [('prop_frames.lz', macro(generated / 'door_frames.h', 'PROP_REGIONAL_DOORS_BASE')),
               ('door_frames.lz', macro(generated / 'door_frames.h', 'PROP_REGIONAL_DOORS_FRAMES')),
               ('explosive_barrels.lz', macro(generated / 'explosive_barrels.h', 'PROP_BARRELEX_FRAMES')),
               ('wall_traps.lz', macro(generated / 'wall_traps.h', 'PROP_WALL_TRAP_FRAMES'))]
    decoded = []
    for name, count in sources:
        pixels = frames(generated / name, 0, count)
        if name == 'prop_frames.lz':
            assert pixels == (generated / 'prop_frames.bin').read_bytes()
        decoded.append(pixels)
    native = b''.join(decoded)
    (out / 'native-frames.bin').write_bytes(native)
    production = (ROOT / 'src/gba_mode0.c').read_text()
    lookup = braced(production, production.index('static inline const uint8_t *prop_tile('))
    blend = braced(production, production.index('static __attribute__((noinline)) void terrain_piece('))
    draw_start = production.index('const uint8_t *pic=prop_tile(prop_draw_frames[prop-1]')
    draw = production[draw_start:production.index('if(pic)', draw_start)]
    draw += braced(production, production.index('if(pic)', draw_start))
    wrapper = HOST + lookup + '\n' + blend + '\n' + WRAPPER + draw + '\n}\n' + CHECK
    source = out / 'check.c'
    source.write_text(wrapper)
    assembly = out / 'atlas.s'
    assembly.write_text('.section .rodata\n.balign 4\n.global asset_prop_atlas\nasset_prop_atlas:\n'
                        '.incbin "' + str(generated / 'prop_atlas.lz') + '"\n'
                        '.section .note.GNU-stack,"",@progbits\n')
    executable = out / 'prop-atlas-check'
    subprocess.run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I' + str(ROOT),
                    str(source), str(assembly), '-o', str(executable)], check=True)
    run = subprocess.run([str(executable), str(out / 'native-frames.bin')], text=True,
                         capture_output=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                                   'UBSAN_OPTIONS': 'halt_on_error=1'})
    if run.returncode:
        print(run.stdout, end='')
        print(run.stderr, end='')
        run.check_returncode()
    report = json.loads(run.stdout)
    report.update({'sanitizers': ['address', 'undefined'],
                   'oracle': 'Independent bounded LZ10 reader; ordinary frame data also matches prop_frames.bin. C oracle uses the original 64-byte row stride.',
                   'production': 'Verbatim current prop_tile, terrain_piece and the full tile lookup/highlight/normal-blend block from stream_dungeon.',
                   'native_decoded_sha256': hashlib.sha256(native).hexdigest(),
                   'production_functions_sha256': hashlib.sha256((lookup + blend + draw).encode()).hexdigest(),
                   'atlas_sha256': hashlib.sha256((generated / 'prop_atlas.lz').read_bytes()).hexdigest(),
                   'header_sha256': hashlib.sha256((generated / 'prop_atlas.h').read_bytes()).hexdigest()})
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
