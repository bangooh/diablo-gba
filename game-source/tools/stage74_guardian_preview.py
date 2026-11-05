"""Original half-scale CL2 versus exact resident composite pixels."""
from pathlib import Path
import re,struct,sys
from PIL import Image,ImageDraw
from extract_assets import cl2_frames
r=Path(__file__).resolve().parents[1];o=r/'generated/stage74-validation';o.mkdir(exist_ok=True)
def atlas(root):
 s=(root/'generated/spell_expansion_assets.h').read_text()
 def rows(name):return re.findall(r'\{([^{}]+)\}',s.split(name,1)[1].split('=',1)[1].split(';',1)[0])
 tiles=[[int(n,16) for n in re.findall(r'0x[0-9a-f]+',row)] for row in rows('spell_small_tiles')]
 geo=[[int(n) for n in row.split(',')] for row in rows('spell_effect_geometry')];comp=[[int(n) for n in row.split(',')] for row in rows('spell_composite')]
 cols=[int(v) for v in s.split('spell_native_colors',1)[1].split('{',1)[1].split('}',1)[0].split(',')];pal=[(0,0,0)]*6+[tuple(((c>>shift)&31)*255//31 for shift in [0,5,10]) for c in cols]
 first,num=comp[3];pts=[(geo[i][4]-8,geo[i][5]-16) for i in range(first,first+num)];x0=min(x for x,y in pts);y0=min(y for x,y in pts);im=Image.new('RGB',(max(x for x,y in pts)-x0+16,max(y for x,y in pts)-y0+16))
 for i,(ox,oy) in zip(range(first,first+num),pts):
  data=struct.pack('<32I',*tiles[geo[i][0]+3]);piece=Image.new('RGB',(16,16));p=piece.load()
  for y in range(16):
   for x in range(16):
    byte=data[(y//8*2+x//8)*32+(y%8)*4+(x%8)//2];p[x,y]=pal[(byte>>(4*(x&1)))&15]
  im.paste(piece,(ox-x0,oy-y0))
 return im
old=atlas(r.parent.parent/'stage73/dungeon-gba');new=atlas(r)
raw=(r/'source_assets/spells/l1.pal').read_bytes();frames=cl2_frames(struct.pack('<8I',*([32]*8))+(r/'source_assets/spell_expansion/Missiles_Guard2.CL2').read_bytes(),96)[0]
boxes=[f.getbbox() for f in frames];box=(min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes));native=frames[3].crop(box);native.putpalette(raw);native=native.convert('RGB').resize(((box[2]-box[0]+1)//2,(box[3]-box[1]+1)//2),Image.Resampling.NEAREST)
orig=Image.new('RGB',new.size);orig.paste(native,((orig.width-native.width)//2,orig.height-native.height))
canvas=Image.new('RGB',(720,340),(20,24,29));d=ImageDraw.Draw(canvas)
for i,(label,im) in enumerate([('Original / half scale',orig),('Stage73',old),('Stage74',new)]):
 d.text((i*240+20,14),label,fill='white');canvas.paste(im.resize((im.width*6,im.height*6),Image.Resampling.NEAREST),(i*240+20,44))
canvas.save(o/'guardian-comparison.png')
