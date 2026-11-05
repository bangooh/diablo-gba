"""Original DT terminal poses, all three classes/armor weights, fixed south-facing corpse."""
from pathlib import Path
import sys,struct,json,hashlib
from PIL import Image
import mpyq
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
from lz77 import indexed,decompress
root=Path(__file__).resolve().parents[1];out=root/'generated';a=mpyq.MPQArchive(sys.argv[1],listfile=False)
pal=read_mpq(a,r'Levels\L1Data\L1_1.PAL');pal=[tuple(pal[i:i+3]) for i in range(0,768,3)]
header=['#pragma once','#include <stdint.h>'];names=[];report=[]
for folder,letter,palette in [('Warrior','W','obj_palette'),('Rogue','R','rogue_palette'),('Sorceror','S','sorcerer_palette')]:
 colors=struct.unpack('<16H',decompress((out/(palette+'.lz')).read_bytes())[:32]);rgb=[tuple(((c>>s)&31)*255//31 for s in [0,5,10]) for c in colors]
 lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
 row=[]
 for armor in 'LMH':
  prefix=letter+armor+'N';path=f'PlrGFX\\{folder}\\{prefix}\\{prefix}DT.CL2';raw=read_mpq(a,path);groups=cl2_frames(raw,128);frames=[]
  for group in groups:
   im=group[-1];canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height));im=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut);frames.append(tile_bytes(im,4))
  frames=[frames[0]]*8 # Terminal south-facing corpse; fixed pose saves ROM.
  name='hero_death_'+prefix.lower();data=indexed(b''.join(frames),2048);(out/(name+'.lz')).write_bytes(data);header.append('extern const uint8_t asset_'+name+'[];');row.append('asset_'+name);report.append({'source':path,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(data),'sourceFrames':len(groups[0])})
 names.append('{'+','.join(row)+'}')
header.append('static const uint8_t *const hero_death_pose[3][3]={'+','.join(names)+'};');(out/'hero_death_pose.h').write_text('\n'.join(header)+'\n');(out/'hero-death152-report.json').write_text(json.dumps(report,indent=2));print(report);print('total',sum(x['bytes'] for x in report))
