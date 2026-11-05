"""Native Rockstan.CEL static frame; append after existing 49 object frames."""
from pathlib import Path
import struct,sys,re,json,hashlib
from PIL import Image
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];src=r/'source_assets/objects';out=r/'generated'
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False);(src/'Rockstan.CEL').write_bytes(read_mpq(a,'Objects\\Rockstan.CEL'))
raw=(src/'Rockstan.CEL').read_bytes();lo,hi=struct.unpack_from('<II',raw,4);frame=raw[lo:hi];p=struct.unpack_from('<H',frame)[0];pixels=bytearray()
while p<len(frame):
 c=frame[p];p+=1
 if c>=128:pixels.extend(bytes(256-c))
 else:pixels.extend(frame[p:p+c]);p+=c
assert len(pixels)%96==0
im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((48,im.height//2),Image.Resampling.NEAREST);assert im.height<=64
canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height))
# Translate the original Catacombs object palette to the shared port BG palette.
pal=(r/'source_assets/worldgen/L2_1.PAL').read_bytes();bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()))
colors=[tuple(((c>>shift)&31)*255//31 for shift in (0,5,10)) for c in bg]
lut=[0]+[min(range(1,256),key=lambda j:sum((colors[j][k]-pal[i*3+k])**2 for k in range(3))) for i in range(1,256)]
canvas=canvas.point(lut)
prop=(out/'prop_frames.bin').read_bytes()[:49*4096];assert len(prop)==49*4096
prop+=canvas.tobytes();(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(prop,4096))
(out/'quest_rock.h').write_text('#pragma once\n#define PROP_ROCK_STAND_FRAME 49\n')
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
(out/'quest-rock-art.json').write_text(json.dumps({'source_sha256':hashlib.sha256(raw).hexdigest(),'native_frame':1,'native_width':96,'frames_total':50,'extra_runtime_ram':0},indent=2)+'\n')
print('Native Rockstan frame appended; 50 prop frames')
