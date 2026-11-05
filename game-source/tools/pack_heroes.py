"""Native class sprites: town ST/WL, dungeon AS/AW/AT, and class portraits."""
from pathlib import Path
import sys,struct,json
from PIL import Image
import mpyq
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import obj_palette,tile_bytes,rgb15
from lz77 import compress,indexed,decompress
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/heroes';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False);raw=read_mpq(a,'levels\\l1data\\l1_1.pal');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)];header=[];report={}
for cls,path,pre in [('warrior','warrior','wls'),('rogue','rogue','rls'),('sorcerer','sorceror','slt')]:
 groups={}
 for st,w in ([('st',96),('wl',96)] if cls=='warrior' else [('as',96),('aw',96),('at',128),('st',96),('wl',96)]):
  name=f'plrgfx\\{path}\\{pre}\\{pre}{st}.cl2';data=read_mpq(a,name);(src/(pre+st+'.cl2')).write_bytes(data);groups[st]=cl2_frames(data,w)
 if cls=='warrior':
  # Match the existing warrior palette without changing dungeon frames.
  colors=struct.unpack('<16H',decompress((out/'obj_palette.lz').read_bytes())[:32])
  palette=[tuple(((c>>shift)&31)*255//31 for shift in (0,5,10)) for c in colors]
  lut=[0]+[min(range(1,16),key=lambda j:sum((palette[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
 else:
  # Keep the established dungeon palette identical across area changes.
  images=[im for st in ('as','aw','at') for group in groups[st] for im in group];palette,lut=obj_palette(images,pal)
 if cls!='warrior':
  (out/(cls+'_palette.lz')).write_bytes(compress(rgb15(palette)));header.append(f'extern const uint8_t asset_{cls}_palette[];')
 for st,directions in groups.items():
  frames=[]
  for direction in directions:
   for im in direction:
    canvas=Image.new('P',(128,128),0);canvas.paste(im,((128-im.width)//2,128-im.height));canvas=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut);frames.append(tile_bytes(canvas,4))
  (out/(cls+'_'+st+'.lz')).write_bytes(indexed(b''.join(frames),2048));header += [f'extern const uint8_t asset_{cls}_{st}[];',f'#define FRAMES_{cls.upper()}_{st.upper()} {len(directions[0])}']
  report[cls+'_'+st]=len(directions[0])
for n in ['selhero.pcx','heros.pcx']:(src/n).write_bytes(read_mpq(a,'ui_art\\'+n))
bg=Image.open(src/'selhero.pcx').convert('RGB').resize((240,160),Image.Resampling.LANCZOS);portraits=Image.open(src/'heros.pcx').convert('RGB')
p=struct.unpack('<256H',decompress((out/'front_palette.lz').read_bytes()));flat=[v for c in p for v in [((c>>s)&31)*255//31 for s in [0,5,10]]];q=Image.new('P',(1,1));q.putpalette(flat)
frames=[]
for i in range(3):
 im=bg.copy();im.paste(portraits.crop((0,i*76,180,(i+1)*76)).resize((67,26),Image.Resampling.LANCZOS),(12,70));im.save(src/f'choice-{i}.png');frames.append(im.quantize(palette=q,dither=Image.Dither.NONE).tobytes())
(out/'hero_choices.lz').write_bytes(indexed(b''.join(frames),38400));header.append('extern const uint8_t asset_hero_choices[];');(out/'heroes.h').write_text('\n'.join(header)+'\n');(out/'heroes-report.json').write_text(json.dumps(report,indent=2));print(report)
