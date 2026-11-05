"""Recreate native closed shortcuts and clean/poisoned well from MPQ tiles.

Town.cpp T_FillTile / T_Pass3 from the pinned Devilution revision supplies
macro IDs and coordinates. Warp 3 uses deterministic tile 1 instead of random
1..4. Base town files remain unchanged; state tiles append to town_tiles.lz.
"""
import argparse,struct,re,json
from pathlib import Path
import mpyq
from PIL import Image
from extract_assets import read_mpq,tile_frame
from pack_gba_assets import mask
from lz77 import indexed
ROOT=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mpq',type=Path,required=True);a=p.parse_args()
 archive=mpyq.MPQArchive(str(a.mpq),listfile=False)
 files={n:read_mpq(archive,'Levels\\TownData\\'+n) for n in ['town.cel','town.min','town.til','town.sol','sector1s.dun','sector2s.dun','sector3s.dun','sector4s.dun']};archive.file.close()
 pieces=[Image.new('P',(32,128))]
 for off in range(0,len(files['town.min']),32):
  im=Image.new('P',(64,256))
  for j,word in enumerate(struct.unpack_from('<16H',files['town.min'],off)):
   tile=tile_frame(files['town.cel'],word);im.paste(tile,((j&1)*32,(j//2)*32),mask(tile))
  pieces.append(im.resize((32,128),Image.Resampling.NEAREST))
 grid=[[0]*96 for _ in range(96)]
 def fill(g,x,y,t):
  vals=[v+1 for v in struct.unpack_from('<4H',files['town.til'],(t-1)*8)] if t else [0]*4
  for k,v in enumerate(vals):g[y+(k>>1)][x+(k&1)]=v
 for sector,ox,oy,w,h in [(1,46,46,25,25),(2,46,0,25,23),(3,0,46,23,25),(4,0,0,23,23)]:
  data=files[f'sector{sector}s.dun'];assert struct.unpack_from('<HH',data)==(w,h)
  for y in range(h):
   for x in range(w):fill(grid,ox+x*2,oy+y*2,struct.unpack_from('<H',data,4+(y*w+x)*2)[0])
 out=ROOT/'generated';raw=(out/'town_tiles.bin').read_bytes();tiles=[raw[i:i+128] for i in range(0,len(raw),128)];lookup={v:i for i,v in enumerate(tiles)}
 base=list(struct.unpack('<79872H',(out/'town_map.bin').read_bytes()));collision=(out/'town_collision.bin').read_bytes();sol=files['town.sol']
 npc=re.search(r'npc_xy[^=]*=\{(.*?)\};',(out/'towners.h').read_text())[1];npc=[tuple(map(int,m)) for m in re.findall(r'\{(\d+),(\d+)\}',npc)][1:8]
 order=sorted(((x,y) for y in range(96) for x in range(96)),key=lambda v:sum(v))
 def walk(n):return n!=0 and (n in (310,311) or not(sol[n-1]&1))
 def state(changes):
  g=[row[:] for row in grid]
  for x,y,t in changes:fill(g,x,y,t)
  world=Image.new('P',(3072,1664));depth=Image.new('I',world.size)
  for x,y in order:
   n=g[y][x]
   if not n:continue
   im=pieces[n];at=(1536+(x-y)*16-16,(x+y)*8);opaque=mask(im);world.paste(im,at,opaque)
   z=Image.new('I',im.size,128+(x+y)*8)
   if walk(n):z.paste(0,(0,112,32,128))
   depth.paste(z,at,opaque)
  changes=[]
  for y in range(208):
   for x in range(384):
    rect=(x*8,y*8,x*8+8,y*8+8);tile=world.crop(rect).tobytes()+bytes(v-y*8 if v else 0 for v in depth.crop(rect).getdata())
    if tile not in lookup:lookup[tile]=len(tiles);tiles.append(tile)
    i=y*384+x;value=lookup[tile]
    if base[i]!=value:changes.append((i,value))
  c=bytearray(int(10<=x<84 and 10<=y<84 and walk(g[y][x])) for y in range(96) for x in range(96))
  for x,y in npc:c[y*96+x]=1
  return changes,[(i,v) for i,v in enumerate(c) if v!=collision[i]]
 def declaration(name,rows):return 'static const uint16_t '+name+'[][2]={'+','.join('{%d,%d}'%v for v in rows)+'};\n'
 shortcut='/* Native town.cpp closed overlays; indices refer to the existing open town. */\n'
 for i,changes in enumerate([[(48,20,320)],[(16,68,332),(16,70,331)],[(x,78,1) for x in range(36,46)]]):
  m,c=state(changes);shortcut+=declaration('town_warp_map'+str(i),m)+declaration('town_warp_collision'+str(i),c)
 shortcut+='static UI_FN void town_shortcuts_apply(uint16_t *map,uint8_t *collision){\n unsigned opened=game_shortcut_mask();\n'
 for i in range(3):
  shortcut+=f' if(!(opened&{1<<i})){{\n'
  for target,prefix in [('map','town_warp_map'),('collision','town_warp_collision')]:
   name=prefix+str(i);shortcut+=f'  if({target})for(unsigned j=0;j<sizeof({name})/sizeof({name}[0]);j++){target}[{name}[j][0]]={name}[j][1];\n'
  shortcut+=' }\n'
 shortcut+='}\n';(out/'town_shortcuts.inc').write_text(shortcut)
 clean,_=state([(60,70,71)]);poisoned,_=state([(60,70,342)])
 well='/* Native clean/poisoned well, same quest state used by entrance and rewards. */\n'+declaration('town_well_clean',clean)+declaration('town_well_poisoned',poisoned)
 well+='static UI_FN void town_well_apply(uint16_t *map){\n if(quest_progress.state[13]>0&&quest_progress.state[13]<3){for(unsigned i=0;i<sizeof(town_well_poisoned)/sizeof(town_well_poisoned[0]);i++)map[town_well_poisoned[i][0]]=town_well_poisoned[i][1];}\n}\n'
 (out/'town_well.inc').write_text(well);(out/'town_tiles.lz').write_bytes(indexed(b''.join(tiles),128))
 print('PASS: appended',len(tiles)-len(raw)//128,'native state tiles')
if __name__=='__main__':main()
