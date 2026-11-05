"""MPQ Panel8 slot and Objcurs potion art -> existing 4bpp HUD tiles."""
from pathlib import Path
from PIL import Image
r=Path(__file__).resolve().parents[1]
# First of the original 29px belt cells, including its stone border.
slot=Image.open(r/'source_assets/control/Panel8-0.png').convert('RGB').crop((204,20,233,51)).resize((8,8),Image.Resampling.LANCZOS)
colors=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),(31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),(2,2,3),(31,31,31),(20,20,20),(0,0,0)]
colors=[tuple(v*255//31 for v in c) for c in colors]
kinds=[0,1,2,3]+list(range(78,85))
tiles=[];preview=Image.new('RGB',(len(kinds)*8,8))
for code,kind in enumerate(kinds):
 im=slot.copy()
 if kind:
  p=Image.open(r/f'source_assets/inventory/item-{kind:02d}.png').convert('RGBA')
  # Source uses black/transparent surrounding the cursor art.
  for y in range(p.height):
   for x in range(p.width):
    c=p.getpixel((x,y))
    if c[:3]==(0,0,0):p.putpixel((x,y),(0,0,0,0))
  # Full heal/mana/rejuvenation are stable IDs78..80, NOT the source IDs.
  size=(6,6) if 78<=kind<=80 else (4,5) if kind<=3 else (4,6)
  box=p.getbbox();p=p.crop(box).resize(size,Image.Resampling.NEAREST)
  im.paste(p,((8-size[0])//2,7-size[1]),p)
 values=[min(range(1,16),key=lambda i:sum((colors[i][k]-c[k])**2 for k in range(3))) for c in im.getdata()]
 tiles.extend(values[i]|values[i+1]<<4 for i in range(0,64,2))
 for n in range(len(kinds)):
  if n==code:
   for y in range(8):
    for x in range(8):preview.putpixel((n*8+x,y),colors[values[y*8+x]])
# The free former border tile is used for a gold underline above selection.
marker=bytes([0x33]*4+[0]*28)
header='#pragma once\nstatic const unsigned char belt_tiles[352] __attribute__((aligned(4)))={'+','.join(map(str,tiles))+'};\nstatic const unsigned char belt_marker[32] __attribute__((aligned(4)))={'+','.join(map(str,marker))+'};\n'
(r/'generated/belt_tiles.h').write_text(header)
preview.resize((800,80),Image.Resampling.NEAREST).save(r/'generated/belt-preview.png')
