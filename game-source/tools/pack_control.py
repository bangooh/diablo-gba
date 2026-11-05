"""Minimal v17-style bar, cropped MPQ glass globes, and MPQ SmalText font."""
from pathlib import Path
from PIL import Image,ImageDraw
import struct,json
r=Path(__file__).resolve().parents[1];src=r/'source_assets/control';out=r/'generated'
base=Image.open(src/'Panel8-0.png').convert('RGB');bulbs=[Image.open(src/f'P8Bulbs-{i}.png').convert('RGB') for i in range(2)]
mask=Image.new('L',(32,32));ImageDraw.Draw(mask).ellipse((0,0,31,31),fill=255)
def small(im):
 b=Image.new('RGB',(240,32))
 for side,x in enumerate([96,464]):
  globe=im.crop((x+12,4,x+76,68)).resize((32,32),Image.Resampling.LANCZOS)
  b.paste(globe,(0 if side==0 else 208,0),mask)
 return b
preview=small(base);preview.save(out/'control-panel.png')
glyphs='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /:-'
frameids=list(range(1,27))+[36]+list(range(27,36))+[0,52,48,37]
assert len(frameids)==len(glyphs)
for region in ['town','dungeon']:
 palette=struct.unpack('<256H',(out/('town_palette.bin' if region=='town' else 'bg_palette.bin')).read_bytes())
 rgb=[tuple(((c>>k)&31)*255//31 for k in (0,5,10)) for c in palette];cache={}
 def encode(im,transparent=False):
  vals=[]
  for pos,c in enumerate(im.getdata()):
   if transparent:
    x,y=pos%im.width,pos//im.width
    gx=x if im.width==32 or x<32 else x-208
    if not (0<=gx<32 and 0<=y<32 and mask.getpixel((gx,y))):vals.append(0);continue
   if c not in cache:cache[c]=min(range(1,256),key=lambda i:sum((rgb[i][k]-c[k])**2 for k in range(3)))
   vals.append(cache[c])
  return bytes(vals)
 def tiled(raw,w,h):return bytes(raw[(ty+y)*w+tx+x] for ty in range(0,h,8) for tx in range(0,w,8) for y in range(8) for x in range(8))
 (out/f'control_panel_{region}.bin').write_bytes(bytes(64)+tiled(encode(preview,True),240,32))
 frames=[]
 for side in range(2):
  for level in range(34):
   im=base.copy();empty=4+round((33-level)*64/33)
   im.paste(bulbs[side].crop((0,0,88,empty)),(96 if side==0 else 464,0))
   resized=small(im);left=0 if side==0 else 208
   frames.append(tiled(encode(resized.crop((left,0,left+32,32)),True),32,32))
 (out/f'control_globes_{region}.bin').write_bytes(b''.join(frames))
 font=bytearray(64)
 for f in frameids:
  tile=Image.new('RGB',(8,8))
  if f:
   glyph=Image.open(src/f'SmalText-{f-1}.png').convert('RGB').resize((7,7),Image.Resampling.LANCZOS)
   tile.paste(glyph,(0,0))
  font.extend(encode(tile))
 (out/f'{region}_font.bin').write_bytes(font)
(out/'control-report.json').write_text(json.dumps({'source':['CtrlPan/Panel8.CEL','CtrlPan/P8Bulbs.CEL','CtrlPan/SmalText.CEL'],'tile_canvas':[240,32],'globes':{'levels':34,'frame_bytes':1024,'hp':[0,128,32,32],'mana':[208,128,32,32]},'text_bar':[32,144,176,16],'font':{'native':[13,11],'resized':[7,7],'cell':[8,8],'glyphs':glyphs},'belt_slots':10,'statues':False,'buttons':False},indent=2))
