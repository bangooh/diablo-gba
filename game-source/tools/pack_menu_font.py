"""Native proportional DiabloUI font42g; index32 transparency, font42.bin widths."""
from pathlib import Path
import sys,math,json
from PIL import Image
import mpyq
from extract_assets import read_mpq
from lz77 import decompress
import struct
r=Path(__file__).resolve().parents[1];src=r/'source_assets/frontend';out=r/'generated'
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
for name in ['font42.bin','font42g.pcx']:(src/name).write_bytes(read_mpq(a,'ui_art\\'+name))
meta=(src/'font42.bin').read_bytes();sheet=Image.open(src/'font42g.pcx');glyphs='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /:-';height=13;stride=16
p=struct.unpack('<256H',decompress((out/'front_palette.lz').read_bytes()));rgb=[tuple(((c>>s)&31)*255//31 for s in [0,5,10]) for c in p]
widths=[];normal=[];bright=[];preview=Image.new('RGB',(240,52));px=5
for char in glyphs:
 code=ord(char);native=meta[code+2];w=max(2,round((native or meta[0])*height/meta[1]));widths.append(w+1)
 part=sheet.crop((0,code*meta[1],min(native+1,sheet.width),(code+1)*meta[1]))
 mask=Image.frombytes('L',part.size,bytes(0 if v==32 else 255 for v in part.tobytes()));part=part.convert('RGBA');part.putalpha(mask);part=part.resize((w,height),Image.Resampling.LANCZOS)
 for light,target in [(1,normal),(1.35,bright)]:
  for y in range(height):
   for x in range(stride):
    if x>=w or not native or part.getpixel((x,y))[3]<90:target.append(0);continue
    c=tuple(min(255,v*light) for v in part.getpixel((x,y))[:3]);idx=min(range(1,252),key=lambda i:sum((rgb[i][k]-c[k])**2 for k in range(3)));target.append(idx)
 (out/'front_font.lz').write_bytes(bytes(normal+bright))
(out/'front_font_widths.lz').write_bytes(bytes(widths))
(out/'frontend_font.h').write_text(f'extern const uint8_t asset_front_font_widths[];\n#define FRONT_FONT_HEIGHT {height}\n#define FRONT_FONT_STRIDE {stride}\n#define FRONT_FONT_BANK {len(normal)}\n')
(out/'menu-font-report.json').write_text(json.dumps({'source':['ui_art/font42g.pcx','ui_art/font42.bin'],'original_height':meta[1],'height':height,'widths':widths,'transparent_index':32,'proportional':True},indent=2))
print('font',len(glyphs),len(normal+bright),max(widths))
