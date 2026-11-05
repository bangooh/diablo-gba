"""Pinned native King set maps, object groups, guards and interaction frames."""
from pathlib import Path
import struct,re,json,hashlib
from PIL import Image
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];src=r/'source_assets/quest_remaining';out=r/'generated'
def dun(name):
 b=(src/(name+'.DUN')).read_bytes();v=struct.unpack('<'+'H'*(len(b)//2),b);return v[0],v[1],v
h=['#pragma once'];report={}
for name in ['SKngDO','SklKng1','SklKng2']:
 w,ht,v=dun(name);h.append('static const uint8_t '+name.lower()+'[]={'+','.join(map(str,v[2:2+w*ht]))+'};')
 report[name]={'width':w,'height':ht,'sha256':hashlib.sha256((src/(name+'.DUN')).read_bytes()).hexdigest()}
source=(r/'source_assets/monster_catalog/monstdat.cpp').read_text()
body=re.search(r'char MonstConvTbl\[\] = \{\s*#endif(.*?)\};',source,re.S)[1].split('#ifdef')[0]
types=[v.strip() for v in re.sub(r'//[^\n]*','',body).split(',') if v.strip()]
mapping={'MT_RSKELAX':10,'MT_RSKELSD':13,'MT_TSKELBW':34,'MT_XSKELBW':35,'MT_RSKELBW':36}
guards=[]
for i,n in enumerate(v[2+5*w*ht:2+9*w*ht]):
 if n and types[n-1]!='0':guards.append((i%(2*w),i//(2*w),mapping[types[n-1]]))
assert len(guards)==74
h.append('static const uint8_t king_guards[][3]={'+','.join('{%d,%d,%d}'%p for p in guards)+'};')
body=re.search(r'int ObjTypeConv\[\] = \{(.*?)\};',(src/'objdat.cpp').read_text(),re.S)[1]
types=[v.strip() for v in body.split(',') if v.strip()]
mapping={'OBJ_LEVER':28,'OBJ_CRUX1':29,'OBJ_CRUX2':30,'OBJ_CRUX3':31,'OBJ_CHEST1':2,'OBJ_CHEST2':4,'OBJ_CHEST3':5}
objects=[]
for i,n in enumerate(v[2+9*w*ht:2+13*w*ht]):
 if n:objects.append((i%(2*w),i//(2*w),mapping[types[n]]))
assert len(objects)==12
h.append('static const uint8_t king_objects[][3]={'+','.join('{%d,%d,%d}'%p for p in objects)+'};')
rects=[(20,7,23,10),(20,14,21,16),(8,1,15,11)]
h.append('static const uint8_t king_gate_rects[3][4]={'+','.join('{%d,%d,%d,%d}'%p for p in rects)+'};')
(out/'king_quest.h').write_text('\n'.join(h)+'\n')
report.update(guards=guards,objects=objects,gate_rects=rects,native_entry=[83,45],native_return=[82,42],native_king=[35,47],border_removed=16)
report['source_files']={n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in ['setmaps.cpp','trigs.cpp','objdat.cpp']}
(src/'king-source-report.json').write_text(json.dumps(report,indent=2)+'\n')
bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()));pal=(r/'source_assets/corpses/l1.pal').read_bytes()
colors=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in bg]
lut=[0]+[min(range(1,256),key=lambda j:sum((colors[j][k]-pal[i*3+k])**2 for k in range(3))) for i in range(1,256)]
prop=(out/'prop_frames.bin').read_bytes()[:62*4096];assert len(prop)==62*4096
for name,frames in [('Lever',[1,2]),('CruxSk1',[1,15]),('CruxSk2',[1,15]),('CruxSk3',[1,15])]:
 raw=(src/(name+'.CEL')).read_bytes()
 for number in frames:
  lo,hi=struct.unpack_from('<II',raw,number*4);frame=raw[lo:hi];pos=struct.unpack_from('<H',frame)[0];pixels=bytearray()
  while pos<len(frame):
   c=frame[pos];pos+=1
   if c>=128:pixels.extend(bytes(256-c))
   else:pixels.extend(frame[pos:pos+c]);pos+=c
  assert len(pixels)%96==0
  im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  im=im.resize((48,im.height//2),Image.Resampling.NEAREST);canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));prop+=canvas.point(lut).tobytes()
(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(prop,4096))
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
print('King: 74 guards, 12 objects, 3 gate groups, 70 prop frames')
