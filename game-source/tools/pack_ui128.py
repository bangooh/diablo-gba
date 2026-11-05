"""Reuse the native inventory frame and retail CharBut frame 2.
Borders keep their original pixels; larger edges repeat instead of stretching.
Inputs are extracted from a user-supplied MPQ by scripts/regenerate_ui.py.
"""
from pathlib import Path
from PIL import Image
import struct
from lz77 import compress

ROOT=Path(__file__).resolve().parents[1]
S=ROOT/'tools/ui_sources/opt128'; G=ROOT/'generated'
native=Image.open(S/'Inv-gba.png').convert('RGB').crop((8,0,128,128))
def frame(w,h):
    im=Image.new('RGB',(w,h))
    for y in range(h):
        sy=y if h==128 or y<3 else 128-(h-y) if y>=h-3 else 3+(y-3)%122
        for x in range(w):
            sx=x if w==120 or x<3 else 120-(w-x) if x>=w-3 else 3+(x-3)%114
            if x<3 or x>=w-3 or y<3 or y>=h-3:im.putpixel((x,y),native.getpixel((sx,sy)))
    return im
raw=(S/'CharBut.CEL').read_bytes();start,end=struct.unpack_from('<II',raw,8)
data=raw[start:end];pixels=bytearray();p=0
while p<len(data):
    n=data[p];p+=1
    if n>=128:pixels.extend(bytes(256-n))
    else:pixels.extend(data[p:p+n]);p+=n
button=Image.frombytes('P',(41,22),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
button.putpalette((S/'town.pal').read_bytes())
button=button.convert('RGB')
# Keep the cross square: crop the native center, retaining the outer vertical
# frame edges separately instead of squashing a 41px button to 10px.
small=button.crop((10,0,31,22)).resize((10,10),Image.Resampling.NEAREST)
for y in range(10):
    small.putpixel((0,y),button.getpixel((0,y*22//10)))
    small.putpixel((9,y),button.getpixel((40,y*22//10)))
button=small
images=[frame(120,128),frame(224,144),button]
def rgb5(c):return (c[0]>>3)|((c[1]>>3)<<5)|((c[2]>>3)<<10)
colors=sorted({rgb5(c) for im in images for c in im.getdata()});lut={c:i for i,c in enumerate(colors)}
border=images[0]
data=bytes(lut[rgb5(border.getpixel((x,y)))] for y in [0,1,2,125,126,127] for x in range(120))
data+=bytes(lut[rgb5(border.getpixel((x,y)))] for y in range(3,125) for x in [0,1,2,117,118,119])
packed=compress(data);(G/'ui128_frames.lz').write_bytes(packed)
(G/'ui128_assets.h').write_text('/* Native inventory border; original CharBut frame 2. */\n'
    +f'#define UI128_COLORS {len(colors)}\nstatic const uint16_t ui128_palette[UI128_COLORS]={{'+','.join(map(str,colors))+'};\n'
    +'extern const uint8_t asset_ui128_frames[];\nstatic const uint8_t ui128_plus[100]={'
    +','.join(str(lut[rgb5(c)]) for c in button.getdata())+'};\n')
for im,name in zip(images,['inventory-frame','spell-frame','stat-plus']):im.save(S/(name+'.png'))
print('frame bytes',len(packed),'colors',len(colors))
