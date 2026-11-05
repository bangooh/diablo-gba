"""Native skeleton axe N/W/A frames and white translation, L1 palette."""
from pathlib import Path
import sys
import mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import obj_palette,tile_bytes,rgb15
root=Path(__file__).resolve().parents[1];src=root/'source_assets/skeleton';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False);palraw=read_mpq(a,'Levels\\L1Data\\L1_1.PAL');pal=[tuple(palraw[i:i+3]) for i in range(0,768,3)]
trn=read_mpq(a,'Monsters\\SkelAxe\\White.TRN');(src/'White.TRN').write_bytes(trn)
groups={}
for state in ['n','w','a']:
 raw=read_mpq(a,'Monsters\\SkelAxe\\SklAx'+state+'.CL2');(src/(state+'.CL2')).write_bytes(raw)
 groups[state]=[[im.point(list(trn)) for im in d] for d in cl2_frames(raw,128)]
palette,lut=obj_palette([im for group in groups.values() for d in group for im in d],pal)
(root/'generated/skeleton_palette.bin').write_bytes(rgb15(palette+[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in palette[1:]]))
header=[]
for state,group in groups.items():
 out=bytearray();count=len(group[0]);header.append(f'#define FRAMES_SKELETON_{state.upper()} {count}')
 for d in group:
  for im in d:
   canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height));canvas=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut);out.extend(tile_bytes(canvas,4))
 (root/f'generated/skeleton_{state}.bin').write_bytes(out)
(root/'generated/floor_actors.h').write_text('\n'.join(header)+'\n')
print(header)
