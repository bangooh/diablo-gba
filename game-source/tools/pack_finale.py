"""Pinned native Vile1/Vile2 and Diablo quadrants: maps, guards and objects."""
from pathlib import Path
import struct,json,hashlib,re,sys,mpyq
from PIL import Image
from extract_assets import read_mpq
from lz77 import indexed
r=Path(__file__).resolve().parents[1];src=r/'source_assets/finale';out=r/'generated';a=mpyq.MPQArchive(sys.argv[1],listfile=False)
header=['#pragma once'];meta={}
for name in ['Vile1','Vile2','diab1','diab2a','diab2b','diab3a','diab3b','diab4a','diab4b']:
 src.mkdir(exist_ok=True);b=read_mpq(a,('Levels\\L1Data\\' if name.startswith('Vile') else 'Levels\\L4Data\\')+name+'.DUN');(src/(name+'.DUN')).write_bytes(b);v=struct.unpack('<'+'H'*(len(b)//2),b);w,h=v[:2]
 header.append('static const uint8_t finale_'+name.lower()+'[]={'+','.join(map(str,v[2:2+w*h]))+'};')
 mons=[];objs=[]
 for i,n in enumerate(v[2+5*w*h:2+9*w*h]):
  if n:mons.append([i%(2*w),i//(2*w),{98:19,101:22,108:45,109:45,113:40,115:44}[n]])
 for i,n in enumerate(v[2+9*w*h:2+13*w*h]):
  if n and n!=52:objs.append([i%(2*w),i//(2*w),{14:34,36:35,37:35,105:36,51:39}[n]])
 if name in ['Vile2','diab1','diab2a','diab3a','diab4a']:
  for label,values in [('guards',mons),('objects',objs)]:
   if values:header.append('static const uint8_t finale_'+name.lower()+'_'+label+'[][3]={'+','.join('{'+','.join(map(str,p))+'}' for p in values)+'};')
 meta[name]={'width':w,'height':h,'guards':mons,'objects':objs,'sha256':hashlib.sha256(b).hexdigest()}
blob=(out/'prop_frames.bin').read_bytes()[:74*4096];assert len(blob)==74*4096
for name,width in [('Book2',96),('Mcirl',96),('Altboy',128),('Switch4',96),('LzStand',128),('Mushptch',96)]:
 raw=read_mpq(a,'Objects\\'+name+'.CEL');(src/(name+'.CEL')).write_bytes(raw);count=struct.unpack_from('<I',raw)[0]
 header.append('#define PROP_FINALE_'+name.upper()+' '+str(len(blob)//4096));header.append('#define FRAMES_FINALE_'+name.upper()+' '+str(count))
 offsets=struct.unpack_from('<'+'I'*(count+1),raw,4)
 for i in range(count):
  data=raw[offsets[i]:offsets[i+1]];pos=struct.unpack_from('<H',data)[0];pixels=bytearray()
  while pos<len(data):
   n=data[pos];pos+=1
   if n>=128:pixels.extend(bytes(256-n))
   else:pixels.extend(data[pos:pos+n]);pos+=n
  assert len(pixels)%width==0,(name,i,width,len(pixels),data[:12])
  im=Image.frombytes('P',(width,len(pixels)//width),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((width//2,im.height//2),Image.Resampling.NEAREST);assert im.height<=64
  canvas=Image.new('P',(64,64));canvas.paste(im,((64-im.width)//2,64-im.height));blob+=canvas.tobytes()
(out/'prop_frames.bin').write_bytes(blob);(out/'prop_frames.lz').write_bytes(indexed(blob,4096));(out/'finale_assets.h').write_text('\n'.join(header)+'\n');(out/'finale-assets-report.json').write_text(json.dumps(meta,indent=2)+'\n');print('Finale native assets: props',len(blob)//4096,'Vile guards',len(meta['Vile2']['guards']))
