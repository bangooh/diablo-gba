#!/usr/bin/env python3
"""Build exact opaque-tile masks from the current floor image stream.

Uses the production floor decoder on the host. The independent Stage58 oracle
checks the complete output against a separate Python asset decoder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--input', type=Path, default=ROOT/'generated/floor_decals.rle')
p.add_argument('--output', type=Path, default=ROOT/'generated/floor_tile_masks.h')
p.add_argument('--report', type=Path, default=ROOT/'generated/floor-tile-masks-report.json')
args = p.parse_args()
item_base = int(re.search(r'FLOOR_ITEM_BASE\s*=\s*(\d+)', (args.input.parent/'floor_decals.h').read_text())[1])
decoder = (ROOT/'src/floor_decode.h').read_text()
harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define UI_FN
DECODER
int main(int argc,char **argv){
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);
 long size=ftell(f);rewind(f);uint8_t *data=malloc((size_t)size);assert(data);
 assert(fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);
 unsigned count=(unsigned)strtoul(argv[2],0,10);assert(count<=*(uint32_t*)data);
 uint32_t canvas[1024];
 for(unsigned frame=0;frame<count;frame++){
  floor_decode(data,frame,(uint8_t*)canvas);
  const uint8_t *pixels=(const uint8_t*)canvas;uint8_t rows[8]={0};
  for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++)if(pixels[y*64+x])rows[y/8]|=(uint8_t)(1u<<(x/8));
  assert(fwrite(rows,1,8,stdout)==8);
 }
 free(data);return 0;
}
'''.replace('DECODER', decoder)
with tempfile.TemporaryDirectory(prefix='floor-tile-masks-') as td:
    td = Path(td)
    (td/'generate.c').write_text(harness)
    subprocess.run(['cc','-std=c11','-O2',str(td/'generate.c'),'-o',str(td/'generate')],check=True)
    masks = subprocess.check_output([str(td/'generate'),str(args.input),str(item_base)])
assert len(masks)==item_base*8
header = '#pragma once\n#include <stdint.h>\n/* Exact opaque 8x8 blocks; generated from floor_decals.rle. */\n'
header += 'static const uint8_t floor_tile_masks[][8]={\n'
header += ''.join(' {'+','.join(str(x) for x in masks[f*8:f*8+8])+'},\n' for f in range(item_base))
header += '};\n_Static_assert(sizeof(floor_tile_masks)/8==FLOOR_ITEM_BASE,"floor tile mask asset count");\n'
args.output.parent.mkdir(parents=True,exist_ok=True)
if not args.output.exists() or args.output.read_text()!=header:
    args.output.write_text(header)
report={'frames':item_base,'maskBytes':len(masks),
        'assetSha256':hashlib.sha256(args.input.read_bytes()).hexdigest(),
        'decoderSha256':hashlib.sha256(decoder.encode()).hexdigest(),
        'maskSha256':hashlib.sha256(masks).hexdigest(),
        'headerSha256':hashlib.sha256(header.encode()).hexdigest()}
args.report.parent.mkdir(parents=True,exist_ok=True)
args.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
