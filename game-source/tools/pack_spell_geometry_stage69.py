"""Preserve native missile bounds/anchors; max32px display via OBJ affine2x."""
from pathlib import Path
import struct,json,hashlib,sys
if "--check" in sys.argv:
 r=Path(__file__).resolve().parents[1];d=json.loads((r/"generated/spell-geometry-stage69-check.json").read_text())
 for name,h in d.items():assert hashlib.sha256((r/name).read_bytes()).hexdigest()==h,name
 print("PASS native missile geometry input/output hashes");sys.exit(0)
from PIL import Image
from extract_assets import cl2_frames
from pack_gba_assets import tile_bytes
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/spell_expansion'
rawpal=(r/'source_assets/spells/l1.pal').read_bytes();rgb=[tuple(rawpal[i:i+3]) for i in range(0,768,3)]
colors=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),(31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),(2,2,3),(31,31,31),(20,20,20),(0,0,0)]
colors=[tuple(c*255//31 for c in row) for row in colors];lut=[0]+[min(range(1,16),key=lambda j:sum((colors[j][k]-rgb[i][k])**2 for k in range(3))) for i in range(1,256)]
files=[('Fireba1',96),('Lghning',96),('Firewal1',128),('Guard2',96),('Inferno',96),('Holy1',96),('Flare',128),('Sklball1',96),('Miniltng',64),('Newexp',96),('Portal2',96),('Manashld',96),('GolemW',96),('Bluexfr',160)]+[('Firerun'+str(i),96) for i in range(1,9)]
art=[];geo=[];audit=[];preview=Image.new('RGB',(22*96,192),'#181b23')
for k,(name,width) in enumerate(files):
 p=src/('Monsters_Golem_GolemW.CL2' if k==12 else 'Missiles_'+name+'.CL2');raw=p.read_bytes();frames=cl2_frames(raw if k==12 else struct.pack('<8I',*([32]*8))+raw,width)[0]
 if name=='Bluexfr':
  backraw=(src/'Missiles_Bluexbk.CL2').read_bytes();back=cl2_frames(struct.pack('<8I',*([32]*8))+backraw,width)[0]
  for i,im in enumerate(frames):
   b=back[i];assert b.size==im.size;im.paste(b,(0,0),b.point([0]+[255]*255).convert('L'))
 chosen=[frames[min(len(frames)-1,n*len(frames)//4)] for n in range(4)]
 boxes=[im.getbbox() for im in chosen if im.getbbox()];box=(min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes))
 w,h=box[2]-box[0],box[3]-box[1];native=((w+1)//2,(h+1)//2);scale=2 if max(native)>16 else 1
 ratio=min(.5,32/max(w,h));tw=max(1,round(w*ratio/scale));th=max(1,round(h*ratio/scale));assert tw<=16 and th<=16
 # Anchor to native baseline and horizontal center, preserving transparent margins.
 x=round(((box[0]+box[2])/2-width/2)*.5)-8*scale
 y=round((box[3]-chosen[0].height)*.5)-16*scale
 geo.append((x,y,scale));audit.append({'name':name,'source_sha256':hashlib.sha256(raw).hexdigest(),'source_frames':len(frames),'source_box':box,'native_half_size':native,'display_size':[tw*scale,th*scale],'offset':[x,y],'affine_scale':scale,'resident_frames':4})
 for j,im in enumerate(chosen):
  canvas=Image.new('P',(16,16));piece=im.crop(box).resize((tw,th),Image.Resampling.NEAREST).point(lut);canvas.paste(piece,((16-tw)//2,16-th));art.append(tile_bytes(canvas,4));canvas.putpalette(sum((list(c) for c in colors),[])+[0]*720)
  if j==2:preview.paste(canvas.convert('RGB').resize((16*scale*2,16*scale*2),Image.Resampling.NEAREST),(k*96,64))
classes={1:0,3:1,4:13,6:2,7:10,11:11,12:0,13:3,14:1,15:2,18:1,20:4,21:12,24:13,29:14,30:8,31:5,35:6,36:7}
h='#pragma once\n#include "spell_icon_colors.h"\n'
h+='static const uint32_t spell_effect_tiles[88][32]={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<32I',b))+'}' for b in art)+'};\n'
h+='static const uint8_t spell_effect_class[37]={'+','.join(str(classes.get(i,0)) for i in range(37))+'};\n'
h+='static const struct {int8_t x,y;uint8_t scale;} spell_effect_geometry[22]={'+','.join('{'+','.join(map(str,g))+'}' for g in geo)+'};\n'
from lz77 import decompress
raw=decompress((out/'bow_arrows.lz').read_bytes());h+='static const uint32_t spell_arrow_tiles['+str(len(raw)//128)+'][32]={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<32I',raw[i:i+128]))+'}' for i in range(0,len(raw),128))+'};\n'
(out/'spell_expansion_assets.h').write_text(h);(out/'spell-geometry-stage69-report.json').write_text(json.dumps(audit,indent=2)+'\n');preview.save(out/'spell-geometry-stage69.png')
print('PASS22 native effect anchors,88 resident images, unchanged128-byte tile footprint')

paths=[out/"spell_expansion_assets.h"]+[src/("Monsters_Golem_GolemW.CL2" if name=="GolemW" else "Missiles_"+name+".CL2") for name,_ in files]+[src/"Missiles_Bluexbk.CL2"]
(out/"spell-geometry-stage69-check.json").write_text(json.dumps({str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+"\n")
