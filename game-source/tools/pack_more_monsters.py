"""Extract native retail floor-one N/W/A CL2 animations, at half resolution."""
from pathlib import Path
import sys,mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import obj_palette,tile_bytes,rgb15
root=Path(__file__).resolve().parents[1]
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
raw=read_mpq(a,'Levels\\L1Data\\L1_1.PAL');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
header=[]
for name,folder,stem,translation in [('fallen_spear','FalSpear','Phall','FallenT.TRN'),('fallen_sword','FalSword','Fall','FallenT.TRN'),('scavenger','Scav','Scav',None),('captain','SkelSd','SklSr','White.TRN')]:
 src=root/'source_assets'/name;src.mkdir(exist_ok=True);(src/'l1.pal').write_bytes(raw)
 prefix='Monsters\\'+folder+'\\';trn=read_mpq(a,prefix+translation) if translation else bytes(range(256))
 (src/'translation.trn').write_bytes(trn);groups={}
 for state in ('nwas' if name.startswith('fallen') else 'nwa'):
  data=read_mpq(a,prefix+stem+state+'.CL2');(src/(state+'.CL2')).write_bytes(data)
  groups[state]=[[im.point(list(trn)) for im in d] for d in cl2_frames(data,128)]
 palette,lut=obj_palette([im for group in groups.values() for d in group for im in d],pal)
 (root/f'generated/{name}_palette.bin').write_bytes(rgb15(palette+[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in palette[1:]]))
 for state,group in groups.items():
  out=bytearray();count=len(group[0]);assert len(group)==8 and all(len(d)==count for d in group)
  header.append(f'#define FRAMES_{name.upper()}_{state.upper()} {count}')
  for d in group:
   for im in d:
    canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height));canvas=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut);out.extend(tile_bytes(canvas,4))
  (root/f'generated/{name}_{state}.bin').write_bytes(out)
(root/'generated/more_monsters.h').write_text('\n'.join(header)+'\n');print('\n'.join(header))
