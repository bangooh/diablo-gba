"""Pack native L1/L2/L3/L4 terrain into read-only ROM atlases (no resident atlas RAM).
MIN records: 10 WORDs in Cathedral/Catacombs/Caves; 16 WORDs in Hell.
All original TIL/SOL indices are retained as 16-bit piece IDs.
"""
from pathlib import Path
import sys,struct,json
import mpyq
from PIL import Image
from extract_assets import read_mpq,tile_frame
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/worldgen'
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
base=read_mpq(a,r'Levels\L1Data\L1_1.PAL');palette=[tuple(base[i:i+3]) for i in range(0,768,3)]
header=['#pragma once','#include <stdint.h>','#include "../src/world.h"']
records=[]
for region in (1,2,3,4):
 files={}
 for ext in ('CEL','MIN','TIL','SOL'):
  name=f'Levels\\L{region}Data\\L{region}.{ext}';files[ext]=read_mpq(a,name);(src/f'L{region}.{ext}').write_bytes(files[ext])
 pal=read_mpq(a,f'Levels\\L{region}Data\\L{region}_1.PAL');(src/f'L{region}_1.PAL').write_bytes(pal)
 rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)]
 lut=[0]+[min(range(1,256),key=lambda j:sum((palette[j][k]-rgb[i][k]*.50)**2 for k in range(3))) for i in range(1,256)]
 words=16 if region==4 else 10;height=words//2*16
 blob=bytearray();atlas={};tiles=[];flags=[];occludes=[];count=len(files['MIN'])//(words*2);assert len(files['MIN'])%(words*2)==0
 for n in range(count):
  im=Image.new('P',(64,height*2))
  for j,w in enumerate(struct.unpack_from('<'+'H'*words,files['MIN'],n*words*2)):
   t=tile_frame(files['CEL'],w);im.paste(t,((j&1)*32,(j//2)*32),Image.frombytes('L',t.size,bytes(255 if v else 0 for v in t.tobytes())))
  occludes.append(int(bool(im.getbbox() and im.getbbox()[1]<im.height-32)))
  data=im.resize((32,height),Image.Resampling.NEAREST).point(lut).tobytes()
  for y in range(height//8):
   for x in range(4):
    block=bytes(data[(y*8+j)*32+x*8+i] for j in range(8) for i in range(8))
    if block not in atlas:atlas[block]=len(blob)//64;blob.extend(block)
    tiles.append(atlas[block]);flags.append(0 if not any(block) else 1 if all(block) else 2)
  if n<80:
   if n==0:sheet=Image.new('RGB',(640,height*10),(12,12,12))
   im=im.resize((32,height),Image.Resampling.NEAREST);im.putpalette(pal);sheet.paste(im.convert('RGB'),((n%20)*32,(n//20)*height))
 sheet.crop((0,0,640,height*4)).save(src/f'L{region}-pieces-preview.png')
 macro=struct.unpack('<'+'H'*(len(files['TIL'])//2),files['TIL']);assert max(macro)<count
 (out/f'world_l{region}.rle').write_bytes(blob)
 h=[f'extern const uint8_t asset_world_l{region}[];']
 for ty,name,vals in [('uint8_t','solid',[v&1 for v in files['SOL'][:count]]),('uint8_t','occludes',occludes),('uint16_t','macros',macro),('uint16_t','tiles',[(t<<2)|f for t,f in zip(tiles,flags)])]:h.append(f'static const {ty} l{region}_{name}[]={{'+','.join(map(str,vals))+'};')
 h.append(f'static const WorldArt l{region}_art={{asset_world_l{region},l{region}_solid,l{region}_occludes,l{region}_macros,l{region}_tiles,{len(macro)//4},{count},{height},0}};')
 (out/f'world_l{region}.inc').write_text('\n'.join(h)+'\n');header.append(f'#include "world_l{region}.inc"')
 records.append({'region':region,'macro_count':len(macro)//4,'piece_count':count,'height':height,'ROM_bytes':len(blob)})
sets=[]
for name in ['diab1','diab2a','diab2b','diab3a','diab3b','diab4a','diab4b']:
 data=read_mpq(a,f'Levels\\L4Data\\{name}.DUN');(src/(name+'.DUN')).write_bytes(data)
 # Generator consumes only the first layer (macro terrain).
 w,h=struct.unpack_from('<HH',data);data=data[:4+w*h*2]
 sets.append('static const BYTE '+name+'[]={'+','.join(map(str,data))+'};')
(out/'diablo_quads.inc').write_text('\n'.join(sets)+'\n');(out/'world_art.h').write_text('\n'.join(header)+'\n')
(src/'terrain-report.json').write_text(json.dumps(records,indent=2));print(records)

import subprocess
subprocess.run([sys.executable,str(r/"tools/pack_king_terrain.py")],check=True)

subprocess.run([sys.executable,str(r/"tools/pack_water_quest.py"),sys.argv[1]],check=True)
