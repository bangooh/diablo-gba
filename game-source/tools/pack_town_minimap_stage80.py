"""Precompute exact Stage63 world-space contour pixels for both wounded-man states."""
from pathlib import Path
import struct,json
root=Path(__file__).resolve().parents[1]
raw=(root/'generated/town_collision.bin').read_bytes();assert len(raw)==9216
W,H=88,44
pool=[bytes(8)];ids={pool[0]:0};maps=[]
def rnd(n):return (n*16+4)//9
for wound in (0,1):
 walk=bytearray(raw);walk[32*96+24]=not wound
 raster=bytearray(W*H*8)
 def opened(x,y):return 0<=x<96 and 0<=y<96 and walk[y*96+x]
 def line(x,y,axis):
  a=2*(rnd(x)-rnd(y));b=2*(rnd(x+(not axis))-rnd(y+axis));k=rnd(x if axis else y)
  for px in range(min(a,b),max(a,b)+1):
   py=((-px if axis else px)+4*k-4)//2;xx=px+352;yy=py+8
   if 0<=xx<W*8 and 0<=yy<H*8:raster[((yy//8)*W+xx//8)*8+yy%8]|=1<<(xx%8)
 for y in range(97):
  for x in range(97):
   if x<96 and opened(x,y)!=opened(x,y-1):line(x,y,0)
   if y<96 and opened(x,y)!=opened(x-1,y):line(x,y,1)
 entries=[]
 for i in range(W*H):
  tile=bytes(raster[i*8:i*8+8])
  if tile not in ids:ids[tile]=len(pool);pool.append(tile)
  entries.append(ids[tile])
 maps.append(entries)
out=['/* Exact200% town contour tiles, world origin(-352,-8). */',f'#define TOWN_MAP80_W {W}',f'#define TOWN_MAP80_H {H}',f'static const uint32_t town_map80_tiles[{len(pool)}][2]={{']
out += ['{0x%08xu,0x%08xu},'%struct.unpack('<II',p) for p in pool];out+=['};',f'static const uint16_t town_map80_refs[2][{W*H}]={{']
for entries in maps:out+=['{'+','.join(map(str,entries))+'},']
out+=['};'];(root/'generated/town_minimap80.h').write_text('\n'.join(out)+'\n')
print(json.dumps({'unique_tiles':len(pool),'bytes':len(pool)*8+W*H*4}))
