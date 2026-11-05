"""Native Warlord and Bone Chamber maps, guards, objects and rack frames."""
from pathlib import Path
import struct,json,sys,mpyq
from PIL import Image
from extract_assets import read_mpq
from lz77 import indexed
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/late_quests';src.mkdir(exist_ok=True);a=mpyq.MPQArchive(sys.argv[1],listfile=False);header=['#pragma once'];report={}
for folder,name in [('L4Data','Warlord'),('L4Data','Warlord2'),('L2Data','Bonestr1'),('L2Data','Bonestr2'),('L2Data','Bonecha1'),('L2Data','Bonecha2')]:
 raw=read_mpq(a,'Levels\\'+folder+'\\'+name+'.DUN');(src/(name+'.DUN')).write_bytes(raw);v=struct.unpack('<'+'H'*(len(raw)//2),raw);w,h=v[:2];key=name.lower()
 header += [f'#define LATE_{key.upper()}_W {w}',f'#define LATE_{key.upper()}_H {h}','static const uint8_t late_'+key+'[]={'+','.join(map(str,raw[:4+2*w*h]))+'};']
 guards=[[i%(2*w),i//(2*w),{28:14,31:49,62:23,100:19,101:22}[n]] for i,n in enumerate(v[2+5*w*h:2+9*w*h]) if n]
 objects=[[i%(2*w),i//(2*w),{14:44,16:0,19:33,51:45,84:5,108:46,109:47}[n]] for i,n in enumerate(v[2+9*w*h:2+13*w*h]) if n and n!=16]
 if guards:header.append('static const uint8_t late_'+key+'_guards[][3]={'+','.join('{'+','.join(map(str,p))+'}' for p in guards)+'};')
 if objects:header.append('static const uint8_t late_'+key+'_objects[][3]={'+','.join('{'+','.join(map(str,p))+'}' for p in objects)+'};')
 report[name]={'w':w,'h':h,'guards':guards,'objects':objects}
blob=(out/'prop_frames.bin').read_bytes()[:91*4096];assert len(blob)==91*4096
for name,width in [('Armstand',96),('WeapStnd',96)]:
 raw=read_mpq(a,'Objects\\'+name+'.CEL');(src/(name+'.CEL')).write_bytes(raw);count=struct.unpack_from('<I',raw)[0];header.append('#define PROP_LATE_'+name.upper()+' '+str(len(blob)//4096));offsets=struct.unpack_from('<'+'I'*(count+1),raw,4)
 for i in range(count):
  data=raw[offsets[i]:offsets[i+1]];pos=struct.unpack_from('<H',data)[0];pixels=bytearray()
  while pos<len(data):
   n=data[pos];pos+=1
   if n>=128:pixels.extend(bytes(256-n))
   else:pixels.extend(data[pos:pos+n]);pos+=n
  assert len(pixels)%width==0
  im=Image.frombytes('P',(width,len(pixels)//width),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((width//2,im.height//2),Image.Resampling.NEAREST);canvas=Image.new('P',(64,64));canvas.paste(im,((64-im.width)//2,64-im.height));blob+=canvas.tobytes()
(out/'prop_frames.bin').write_bytes(blob);(out/'prop_frames.lz').write_bytes(indexed(blob,4096));(out/'late_quests.h').write_text('\n'.join(header)+'\n');(out/'late-quests-report.json').write_text(json.dumps(report,indent=2)+'\n');print('Late quest maps and racks',len(blob)//4096,'prop frames')

# The final property stream has more frames than the earlier compressed set.
import re
packed=out/"packed_assets.h"
packed.write_text(re.sub(r"(#define RAW_PROP_FRAMES\s+)\d+", lambda m:m[1]+str(len(blob)), packed.read_text()))
