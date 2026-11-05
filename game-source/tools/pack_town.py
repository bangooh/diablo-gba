"""Reconstruct Diablo's four town sectors using T_FillSector / SetTownMicros."""
from pathlib import Path
from PIL import Image,ImageDraw
import sys,struct,json,hashlib
from extract_assets import read_mpq,tile_frame
from pack_gba_assets import mask,rgb15,tile_bytes,obj_palette
import mpyq

def main():
 mpq,out=Path(sys.argv[1]),Path(sys.argv[2]);out.mkdir(exist_ok=True)
 archive=mpyq.MPQArchive(str(mpq),listfile=False);manifest=[]
 def read(name):
  b=read_mpq(archive,name);manifest.append({'name':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()});return b
 files={n:read('levels\\towndata\\'+n) for n in ['town.cel','town.min','town.til','town.sol','town.pal','sector1s.dun','sector2s.dun','sector3s.dun','sector4s.dun']}
 raw=files['town.pal'];pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
 mini=files['town.min'];pieces=[Image.new('P',(32,128),0)]
 for offset in range(0,len(mini),32):
  im=Image.new('P',(64,256),0)
  for j,w in enumerate(struct.unpack_from('<16H',mini,offset)):
   t=tile_frame(files['town.cel'],w);im.paste(t,((j&1)*32,(j//2)*32),mask(t))
  pieces.append(im.resize((32,128),Image.Resampling.NEAREST))
 grid=[[0]*96 for _ in range(96)]
 for sector,ox,oy,w,h in [(1,46,46,25,25),(2,46,0,25,23),(3,0,46,23,25),(4,0,0,23,23)]:
  data=files[f'sector{sector}s.dun'];assert struct.unpack_from('<HH',data)==(w,h)
  for y in range(h):
   for x in range(w):
    n=struct.unpack_from('<H',data,4+(y*w+x)*2)[0]
    vals=[v+1 for v in struct.unpack_from('<4H',files['town.til'],(n-1)*8)] if n else [0]*4
    for k,v in enumerate(vals):grid[oy+y*2+(k>>1)][ox+x*2+(k&1)]=v
 world=Image.new('P',(3072,1664),0);world.putpalette(raw)
 depth=Image.new('I',world.size,0);sol=files['town.sol']
 # The upper deck halves310/311 share the river SOL bit, narrowing all three bridges.
 # Their rendered floor supports the same two-lane crossing as312/313.
 def walk_piece(n):return n!=0 and (n in (310,311) or not(sol[n-1]&1))
 for x,y in sorted(((x,y) for y in range(96) for x in range(96)),key=lambda p:sum(p)):
  n=grid[y][x]
  if n:
   im=pieces[n];position=(1536+(x-y)*16-16,(x+y)*8);opaque=mask(im)
   world.paste(im,position,opaque)
   # Each painted pixel retains the depth of its original MIN piece.
   # Walkable floor rows cannot occlude actors; upper scenery and solid bases can.
   z=Image.new('I',im.size,128+(x+y)*8)
   if walk_piece(n):z.paste(0,(0,112,32,128))
   depth.paste(z,position,opaque)
 world.save(out/'tristram-world.png',transparency=0)
 depth.convert('I;16').save(out/'tristram-depth.png')
 sol=files['town.sol'];collision=bytes(int(10<=x<84 and 10<=y<84 and walk_piece(grid[y][x])) for y in range(96) for x in range(96))
 tiles=[bytes(128)];lookup={tiles[0]:0};indices=[]
 for y in range(208):
  for x in range(384):
   b=world.crop((x*8,y*8,x*8+8,y*8+8)).tobytes()
   values=list(depth.crop((x*8,y*8,x*8+8,y*8+8)).getdata())
   b+=bytes(v-y*8 if v else 0 for v in values)
   if b not in lookup:lookup[b]=len(tiles);tiles.append(b)
   indices.append(lookup[b])
 assert len(tiles)<65536
 (out/'town_tiles.bin').write_bytes(b''.join(tiles));(out/'town_map.bin').write_bytes(struct.pack('<'+str(len(indices))+'H',*indices))
 (out/'town_collision.bin').write_bytes(collision);(out/'town_palette.bin').write_bytes(rgb15(pal))
 # HUD shares the town palette but lives in its own character block.
 font=bytearray(64);glyphs='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /:-'
 fg=min(range(1,256),key=lambda i:sum((pal[i][k]-230)**2 for k in range(3)));bg=min(range(1,256),key=lambda i:sum((pal[i][k]-12)**2 for k in range(3)))
 for ch in glyphs:
  im=Image.new('P',(8,8),bg);ImageDraw.Draw(im).text((1,-2),ch,fill=fg,font_size=8);font.extend(im.tobytes())
 (out/'town_font.bin').write_bytes(font)
 (out/'town-manifest.json').write_text(json.dumps(manifest,indent=2))
 (out/'town-report.json').write_text(json.dumps({'pieces':len(pieces)-1,'unique_tiles':len(tiles),'world_size':[3072,1664],'walkable_cells':sum(collision),'spawn_walkable':collision[68*96+75],'entry_walkable':collision[29*96+25]},indent=2))
 print((out/'town-report.json').read_text())
if __name__=='__main__':main()
