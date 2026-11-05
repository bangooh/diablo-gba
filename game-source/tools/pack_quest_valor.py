"""Native Book1 frames4/5 and Pedistl frames1..4, following Rockstan."""
from pathlib import Path
import struct,sys,re,json,hashlib
from PIL import Image
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];src=r/'source_assets/objects';out=r/'generated'
names=('Book1','Pedistl')
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
 for name in names:(src/(name+'.CEL')).write_bytes(read_mpq(a,'Objects\\'+name+'.CEL'))
pal=(r/'source_assets/worldgen/L2_1.PAL').read_bytes();bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()))
colors=[tuple(((c>>shift)&31)*255//31 for shift in (0,5,10)) for c in bg]
lut=[0]+[min(range(1,256),key=lambda j:sum((colors[j][k]-pal[i*3+k])**2 for k in range(3))) for i in range(1,256)]
prop=(out/'prop_frames.bin').read_bytes()[:50*4096];assert len(prop)==50*4096
report=[]
for name,frames in [('Book1',[4,5]),('Pedistl',[1,2,3,4])]:
 raw=(src/(name+'.CEL')).read_bytes()
 for number in frames:
  lo,hi=struct.unpack_from('<II',raw,number*4);frame=raw[lo:hi];p=struct.unpack_from('<H',frame)[0];pixels=bytearray()
  while p<len(frame):
   c=frame[p];p+=1
   if c>=128:pixels.extend(bytes(256-c))
   else:pixels.extend(frame[p:p+c]);p+=c
  assert len(pixels)%96==0
  im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((48,im.height//2),Image.Resampling.NEAREST);assert im.height<=64
  canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));canvas=canvas.point(lut);prop+=canvas.tobytes()
  report.append({'file':name,'frame':number,'sha256':hashlib.sha256(raw).hexdigest()})
(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(prop,4096))
(out/'quest_valor.h').write_text('#pragma once\n#define PROP_VALOR_BOOK_FRAME 50\n#define PROP_VALOR_PEDESTAL_FRAME 52\n')
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
(out/'quest-valor-art.json').write_text(json.dumps(report,indent=2)+'\n')
print('Native Valor book/pedestal frames appended; 56 prop frames')
