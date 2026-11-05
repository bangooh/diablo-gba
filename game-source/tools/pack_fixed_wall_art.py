"""Original-texture50% wall art with an intact isometric ground diamond.

Only fixed occluding halves use this bank. Preserve source colors and silhouettes
so arches, columns and wall boundaries remain recognizable.
"""
from pathlib import Path
import re,struct,json
r=Path(__file__).resolve().parents[1]
raw=(r/'generated/dungeon_art_raw.rle').read_bytes()
s=(r/'generated/dungeon_art.h').read_text()
def array(name):
 return [int(v,0) for v in re.search(name+r'\[\]\s*=\s*\{([^}]+)',s)[1].split(',') if v.strip()]
ids=array('dart_tile_ids');count=len(ids)//40
occludes=array('dart_occludes')
used={ids[v*40+t] for v in range(count) if occludes[v] for t in range(32)}
parts=[];lookup={}
def add(tile):
 tile=bytes(tile)
 if tile not in lookup:lookup[tile]=len(parts);parts.append(tile)
 return lookup[tile]
empty=add(bytes(64))
upper=[]
for side in range(2):
 upper.append([add(raw[t*64+p] if raw[t*64+p] and not ((p%8+p//8)&1) else 0 for p in range(64)) if t in used else empty for t in range(len(raw)//64)])
lower=[];preserved=0
for v in range(count):
 for row in range(8,10):
  for col in range(4):
   if not occludes[v]:lower.append(empty);continue
   src=raw[ids[v*40+row*4+col]*64:ids[v*40+row*4+col]*64+64];out=[]
   for p,value in enumerate(src):
    x=col*8+p%8;y=row*8+p//8;floor_top=64+abs(x-16)//2
    if y>=floor_top:out.append(value);preserved+=1
    else:out.append(value if value and not ((x+y)&1) else 0)
   lower.append(add(out))
words=struct.unpack('<'+'I'*(len(parts)*16),b''.join(parts))
header='#pragma once\n/* Generated original-texture fixed walls; ground diamond remains opaque. */\n'
header+='static const uint32_t fixed_wall_words[]={'+','.join(hex(x)+'u' for x in words)+'};\n'
header+='static const uint16_t fixed_wall_upper[2]['+str(len(upper[0]))+']={'+','.join('{'+','.join(map(str,a))+'}' for a in upper)+'};\n'
header+='static const uint16_t fixed_wall_lower[]={'+','.join(map(str,lower))+'};\n'
header+='static const uint8_t fixed_wall_checker[]={'+','.join(str(int(all(bool(value)==((i%8+i//8)%2==0) for i,value in enumerate(tile)))) for tile in parts)+'};\n'
header+='#define fixed_wall_art ((const uint8_t*)fixed_wall_words)\n'
(r/'generated/fixed_wall_art.h').write_text(header)
report={'unique_tiles':len(parts),'art_bytes':len(parts)*64,'index_bytes':(sum(map(len,upper))+len(lower))*2,'ground_pixels_preserved':preserved,'rule':'dy <64+abs(dx-16)/2: original texture checkerboard; otherwise original ground'}
(r/'generated/fixed-wall-report.json').write_text(json.dumps(report,indent=2));print(report)
