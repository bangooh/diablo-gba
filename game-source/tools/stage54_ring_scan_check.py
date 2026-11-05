#!/usr/bin/env python3
"""Bounded equivalence check for the actual dungeon ring traversal only."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract_scan(source):
    start = source.index('static void stream_dungeon(void){')
    prefix_start = source.index('unsigned ring_row=render_row(ty),first_column=render_column(tx);', start)
    prefix_end = source.index('unsigned geometry=', prefix_start)
    suffix = re.search(r'\}\s*ring_row\+=31;if\(ring_row==WORLD_TILES\)ring_row=0;\s*\}',
                       source[prefix_end:])
    assert suffix, 'production ring loop changed; update extraction explicitly'
    # Keep both nested loop headers, slot calculation, both wrap increments,
    # and closing braces verbatim; replace only the rendering payload.
    return source[prefix_start:prefix_end] + 'check_slot(tx,ty,x,y,slot,seen,&visited);\n' + suffix.group()


HOST = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "src/render_coords.h"
enum { WORLD_TILES=21*31 };
static uint64_t origins, slots, negative_origins;
static unsigned normalized(int value,int divisor){
 int result=value%divisor;return (unsigned)(result<0?result+divisor:result);
}
static int floor8(int value){
 int result=value/8;return result-(value<0&&value%8!=0);
}
static void check_slot(int tx,int ty,int x,int y,unsigned slot,
                       uint8_t seen[WORLD_TILES],unsigned *visited){
 unsigned oracle=render_row(y)+render_column(x);
 assert(oracle==normalized(y,21)*31+normalized(x,31));
 assert(slot==oracle && slot<WORLD_TILES);
 assert(x==tx+(int)(*visited%31));
 assert(y==ty+(int)(*visited/31));
 assert(!seen[slot]);seen[slot]=1;(*visited)++;slots++;
}
static void production_scan(int tx,int ty){
 uint8_t seen[WORLD_TILES]={0};unsigned visited=0;
/* PRODUCTION_SCAN */
 assert(visited==WORLD_TILES);
 for(unsigned i=0;i<WORLD_TILES;i++)assert(seen[i]==1);
 origins++;negative_origins+=tx<0||ty<0;
}
int main(void){
 /* Every integer camera pixel in the map's rectangular envelope maps to one
  * tested tile origin. Checking conversion by axis avoids repeating the same
  * 651-slot traversal for all64 subpixel-origin combinations. The envelope
  * also includes positions outside the isometric map diamond. */
 const int xmin=CAMERA_X_MIN,xmax=CAMERA_X_MAX;
 const int ymin=CAMERA_Y_MIN,ymax=CAMERA_Y_MAX;
 unsigned camera_axis_cases=0;
 for(int x=xmin;x<=xmax;x++){assert(render_tile(x)==floor8(x));camera_axis_cases++;}
 for(int y=ymin;y<=ymax;y++){assert(render_tile(y)==floor8(y));camera_axis_cases++;}
 for(int ty=floor8(ymin);ty<=floor8(ymax);ty++)
  for(int tx=floor8(xmin);tx<=floor8(xmax);tx++)production_scan(tx,ty);
 uint64_t world_origins=origins;
 /* Cross both moduli, the origin, and large signed coordinates. Keep loop
  * additions within int range, matching production's integer contract. */
 static const int edge[]={-32768,-32767,-652,-651,-650,-63,-62,-61,-43,-42,-41,
                          -32,-31,-30,-22,-21,-20,-9,-8,-7,-2,-1,0,1,2,7,8,9,
                          20,21,22,30,31,32,41,42,43,61,62,63,650,651,652,32767};
 for(unsigned yi=0;yi<sizeof(edge)/sizeof(edge[0]);yi++)
  for(unsigned xi=0;xi<sizeof(edge)/sizeof(edge[0]);xi++)production_scan(edge[xi],edge[yi]);
 for(int p=-520;p<=520;p++){assert(render_tile(p)==floor8(p));camera_axis_cases++;}
 printf("{\"status\":\"PASS\",\"world_tile_origins\":%llu,"
        "\"extra_signed_origins\":%llu,\"negative_origins\":%llu,"
        "\"slot_comparisons\":%llu,\"camera_axis_cases\":%u,"
        "\"covered_integer_pixel_camera_origins\":%llu}\n",
        (unsigned long long)world_origins,(unsigned long long)(origins-world_origins),
        (unsigned long long)negative_origins,(unsigned long long)slots,camera_axis_cases,
        (unsigned long long)(xmax-xmin+1)*(unsigned long long)(ymax-ymin+1));
 return 0;
}
'''


def main():
    source = (ROOT / 'src/gba_mode0.c').read_text()
    header = (ROOT / 'src/render_coords.h').read_text()
    game = (ROOT / 'src/game.h').read_text()
    dimensions = re.search(r'enum \{ MAP_W=(\d+), MAP_H=(\d+).*?DUNGEON_ORIGIN=(\d+) \};', game)
    assert dimensions
    width, height, origin = map(int, dimensions.groups())
    assert 'cam_x=x-120;cam_y=y-84;' in source
    assert '((game.area?1536:DUNGEON_ORIGIN)+(x-y)*16)<<8' in source
    assert '((game.area?128:72)+(x+y)*8)<<8' in source
    bounds = {'CAMERA_X_MIN': origin - (height - 1) * 16 - 120,
              'CAMERA_X_MAX': origin + (width - 1) * 16 - 120,
              'CAMERA_Y_MIN': 72 - 84,
              'CAMERA_Y_MAX': 72 + (width + height - 2) * 8 - 84}
    scan = extract_scan(source)
    world_tiles = re.search(r'enum \{ WORLD_TILES=21\*31 \};', source)
    assert world_tiles, 'production ring dimensions changed'
    body = HOST.replace('/* PRODUCTION_SCAN */', scan)
    body = '\n'.join(f'#define {key} {value}' for key, value in bounds.items()) + '\n' + body
    work = ROOT / 'work/stage54/ring-scan-host'
    work.mkdir(parents=True, exist_ok=True)
    cfile, executable = work / 'check.c', work / 'check'
    cfile.write_text(body)
    subprocess.run(['gcc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I' + str(ROOT),
                    str(cfile), '-o', str(executable)], check=True)
    run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=60,
                         env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                              'UBSAN_OPTIONS': 'halt_on_error=1'})
    if run.returncode:
        print(run.stdout, end='')
        print(run.stderr, end='')
        run.check_returncode()
    report = json.loads(run.stdout)
    report.update({'sanitizers': ['address', 'undefined'],
                   'camera_bounds_inclusive': bounds,
                   'method': 'Verbatim production nested ring loop with rendering payload replaced by checks. Every distinct tile origin in the full world camera envelope; all pixel-to-tile origins proven by exhaustive axis checks. Oracle is existing render_row/render_column plus independent normalized modulo.',
                   'checks': ['exact oracle slot', 'slot bounds', 'x/y visitation order',
                              'all651 slots visited exactly once', 'signed pixel floor conversion'],
                   'production_scan_sha256': hashlib.sha256(scan.encode()).hexdigest(),
                   'render_coords_sha256': hashlib.sha256(header.encode()).hexdigest()})
    destination = ROOT / 'generated/stage54-validation/barrels/ring-scan-host.json'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
