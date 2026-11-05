"""Native object CEL frames at half resolution. Optional MPQ refresh argument.
Sarc5, Chest1 six frames (two three-frame variants), Barrel9.
Frames retain the original palette indices; the runtime uses the L1 BG palette.
"""
from pathlib import Path
import struct,sys
from PIL import Image
root=Path(__file__).resolve().parents[1];src=root/'source_assets/objects';src.mkdir(exist_ok=True)
archive=None
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
out=bytearray();header=[]
for name,width,expected in [('Sarc',128,5),('Chest1',96,6),('Barrel',96,9)]:
 raw=read_mpq(archive,'Objects\\'+name+'.CEL') if archive else (src/(name+'.CEL')).read_bytes()
 if archive:(src/(name+'.CEL')).write_bytes(raw)
 count=struct.unpack_from('<I',raw)[0];assert count==expected
 offs=struct.unpack_from('<'+str(count+1)+'I',raw,4)
 header.extend([f'#define PROP_{name.upper()}_BASE {len(out)//4096}',f'#define PROP_{name.upper()}_FRAMES {count}'])
 for idx in range(count):
  data=raw[offs[idx]:offs[idx+1]];p=struct.unpack_from('<H',data)[0];pixels=bytearray()
  while p<len(data):
   n=data[p];p+=1
   if n>=128:pixels.extend(bytes(256-n))
   else:pixels.extend(data[p:p+n]);p+=n
  assert len(pixels)%width==0
  im=Image.frombytes('P',(width,len(pixels)//width),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  im=im.resize((width//2,im.height//2),Image.Resampling.NEAREST)
  assert im.height<=64
  canvas=Image.new('P',(64,64));canvas.paste(im,((64-im.width)//2,64-im.height));out.extend(canvas.tobytes())
(root/'generated/prop_frames.bin').write_bytes(out)
(root/'generated/prop_frames.h').write_text('\n'.join(header)+'\n')
print('Native frames:',len(out)//4096,'raw bytes:',len(out))
