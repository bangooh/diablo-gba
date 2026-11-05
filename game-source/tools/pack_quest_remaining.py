"""Native Blind book and non-hostile Lachdanan standing/corpse prop frames."""
from pathlib import Path
import struct,sys,re,json,hashlib
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];src=r/'source_assets/objects';out=r/'generated'
if len(sys.argv)>1:
 import mpyq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
 for local,path in [('Book1.CEL','Objects\\Book1.CEL'),('BHKA.TRN','Monsters\\Monsters\\BHKA.TRN'),('LachdananN.CL2','Monsters\\Black\\Blackn.CL2'),('LachdananD.CL2','Monsters\\Black\\Blackd.CL2')]:
  (src/local).write_bytes(read_mpq(a,path))
bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()))
colors=[tuple(((c>>shift)&31)*255//31 for shift in (0,5,10)) for c in bg]
def lut(name):
 pal=((r/'source_assets/corpses/l1.pal') if name=='L1_1.PAL' else r/'source_assets/worldgen'/name).read_bytes()
 return [0]+[min(range(1,256),key=lambda j:sum((colors[j][k]-pal[i*3+k])**2 for k in range(3))) for i in range(1,256)]
prop=(out/'prop_frames.bin').read_bytes()[:56*4096];assert len(prop)==56*4096
raw=(src/'Book1.CEL').read_bytes()
for number in [1,2]:
 lo,hi=struct.unpack_from('<II',raw,number*4);frame=raw[lo:hi];p=struct.unpack_from('<H',frame)[0];pixels=bytearray()
 while p<len(frame):
  c=frame[p];p+=1
  if c>=128:pixels.extend(bytes(256-c))
  else:pixels.extend(frame[p:p+c]);p+=c
 im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((48,im.height//2),Image.Resampling.NEAREST)
 canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));prop+=canvas.point(lut('L2_1.PAL')).tobytes()
translation=list((src/'BHKA.TRN').read_bytes())
for name,index in [('LachdananN.CL2',0),('LachdananD.CL2',-1)]:
 im=cl2_frames((src/name).read_bytes(),160)[0][index].point(translation)
 side=max(160,im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));prop+=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut('L1_1.PAL')).tobytes()
(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(prop,4096))
(out/'quest_remaining.h').write_text('#pragma once\n#define PROP_BLIND_BOOK_FRAME 56\n#define PROP_LACHDANAN_FRAME 58\n')
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
(out/'quest-remaining-art.json').write_text(json.dumps({'prop_frames':60,'book_frames':[1,2],'Lachdanan':{'native_type':'MT_RBLACK','translation':'BHKA','presentation':'static standing/death frames'},'sha256':{n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in ['Book1.CEL','BHKA.TRN','LachdananN.CL2','LachdananD.CL2']}},indent=2)+'\n')
print('Blind book and Lachdanan native frames appended; 60 prop frames')
