"""OBJ_STORYCANDLE / Candle2: four native96px CEL frames at half resolution."""
from pathlib import Path
import struct,sys,json,hashlib
from PIL import Image
from lz77 import indexed
r=Path(__file__).resolve().parents[1];src=r/'source_assets/quest_remaining';out=r/'generated';p=src/'Candle2.CEL'
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 p.write_bytes(read_mpq(mpyq.MPQArchive(sys.argv[1],listfile=False),'Objects\\Candle2.CEL'))
raw=p.read_bytes();count=struct.unpack_from('<I',raw)[0];assert count==4;offsets=struct.unpack_from('<5I',raw,4);frames=[]
for i in range(4):
 data=raw[offsets[i]:offsets[i+1]];pos=struct.unpack_from('<H',data)[0];pixels=bytearray()
 while pos<len(data):
  n=data[pos];pos+=1
  if n>=128:pixels.extend(bytes(256-n))
  else:pixels.extend(data[pos:pos+n]);pos+=n
 assert len(pixels)%96==0
 im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((48,im.height//2),Image.Resampling.NEAREST)
 assert im.height<=64
 canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));frames.append(canvas.tobytes())
blob=(out/'prop_frames.bin').read_bytes()[:70*4096];assert len(blob)==70*4096
blob+=b''.join(frames);(out/'prop_frames.bin').write_bytes(blob);(out/'prop_frames.lz').write_bytes(indexed(blob,4096))
(out/'water_candles.h').write_text('#pragma once\n#define PROP_WATER_CANDLE_BASE 70\n#define PROP_WATER_CANDLE_FRAMES 4\n')
report={'native_object':'OBJ_STORYCANDLE','source':'Objects\\Candle2.CEL','source_sha256':hashlib.sha256(raw).hexdigest(),'frame_base':70,'frames':4,'delay_ticks':2,'light_radius':3,'solid':True,'selectable':False,'entry_offsets':[[-2,1],[3,1],[-1,2],[2,2]],'decoded_sha256':hashlib.sha256(b''.join(frames)).hexdigest()}
(out/'water-candles-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
