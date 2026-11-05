"""Adapt original Char.CEL stonework, Quest.CEL frame and Diabsmal.CEL logo.
Keep text native and redraw at GBA resolution; never squash the original labels.
Inputs are lossless CEL decodes in ui_sources. No edits to gameplay assets.
"""
from pathlib import Path
from PIL import Image
import struct
from lz77 import indexed
ROOT=Path(__file__).resolve().parents[1];S=ROOT/'tools/ui_sources';G=ROOT/'generated'
char=Image.open(S/'Char.CEL.png').convert('RGB');quest=Image.open(S/'Quest.CEL.png').convert('RGB')
# A clean patch from the original stone, tiled under new readable value boxes.
patch=char.crop((18,80,50,112));panel=Image.new('RGB',(224,144))
for y in range(0,144,32):
 for x in range(0,224,32):panel.paste(patch,(x,y))
frame=quest.resize((224,144),Image.Resampling.NEAREST)
# Repeat the original frame's middle strip; decorative corners stay intact.
# The indexed packer shares these identical strips without changing pixels.
frame_source=frame.copy()
for y in range(8,136):
 for x in range(224):
  frame.putpixel((x,y),frame_source.getpixel((x,16+y%8)))
for y in range(144):
 for x in range(224):
  if x<5 or x>=219 or y<5 or y>=139:panel.putpixel((x,y),frame.getpixel((x,y)))
logo=Image.new('RGB',(224,144));logo.paste(Image.open(S/'Diabsmal.CEL.png').convert('RGB').resize((108,36),Image.Resampling.LANCZOS),(58,2))
images=[panel,frame,logo]
atlas=Image.new('RGB',(224,432))
for i,im in enumerate(images):atlas.paste(im,(0,i*144))
p=atlas.quantize(colors=32,dither=Image.Dither.NONE);pal=p.getpalette()[:96]
colors=[(pal[i]>>3)|((pal[i+1]>>3)<<5)|((pal[i+2]>>3)<<10) for i in range(0,96,3)]
(G/'ui127_assets.h').write_text('/* Original MPQ art; 32-color indexed strip blocks. */\nstatic const uint16_t ui127_palette[32]={'+','.join(map(str,colors))+'};\nextern const uint8_t asset_ui127_panels[];\n')
data=bytearray()
for i in range(3):
 im=p.crop((0,i*144,224,(i+1)*144))
 for row in range(18):
  for tx in range(28):
   for y in range(8):
    for x in range(8):data.append(im.getpixel((tx*8+x,row*8+y)))
 im.save(S/f'preview-{i}.png')
packed=indexed(bytes(data),1792);(G/'ui127_panels.lz').write_bytes(packed);print(len(packed))
# Permit clean panel regeneration before the frontend cursor dependency is built.
import sys
if '--panels-only' in sys.argv:
 sys.exit(0)
# The exact frontend selector pixels, encoded for two hardware OBJ sprites.
import re
from lz77 import decompress
focus=G/'frontend_focus.h'
h=(focus if focus.exists() else G/'frontend_stage81.h').read_text();raw=h.split('front_focus81[8][100]={',1)[1].split('};',1)[0]
frames=[list(map(int,re.findall(r'\d+',s))) for s in re.findall(r'\{([^{}]+)\}',raw)]
front=struct.unpack('<256H',decompress((G/'front_palette.lz').read_bytes()))
im=Image.new('RGB',(16,128));mask=[]
for f,a in enumerate(frames):
 for i,c in enumerate(a):
  if c:
   v=front[c];im.putpixel((i%10,f*16+i//10),((v&31)*8,((v>>5)&31)*8,((v>>10)&31)*8))
q=im.quantize(colors=15,dither=Image.Dither.NONE);pal=q.getpalette();rgb=[0]+[(pal[i]>>3)|((pal[i+1]>>3)<<5)|((pal[i+2]>>3)<<10) for i in range(0,45,3)]
tiles=[]
for f,a in enumerate(frames):
 data=[]
 for ty in range(2):
  for tx in range(2):
   for y in range(8):
    for x in range(0,8,2):
     pixels=[]
     for dx in range(2):
      xx=tx*8+x+dx;yy=ty*8+y;pixels.append(q.getpixel((xx,f*16+yy))+1 if xx<10 and yy<10 and a[yy*10+xx] else 0)
     data.append(pixels[0]|pixels[1]<<4)
 tiles.append(data)
(G/'ui127_cursor.h').write_text('/* Same frontend 10x10 frames, OBJ-encoded. */\nstatic const uint16_t panel_cursor_palette[16]={'+','.join(map(str,rgb))+'};\nstatic const uint8_t panel_cursor_tiles[8][128] __attribute__((aligned(4)))={'+','.join('{'+','.join(map(str,r))+'}' for r in tiles)+'};\n')
