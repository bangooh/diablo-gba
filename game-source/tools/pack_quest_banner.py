"""Native BNG-translated Snotspill standing frame for the pre-combat talk prop."""
from pathlib import Path
import json,struct,re
from PIL import Image
from extract_assets import cl2_frames
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];src=r/'source_assets/monster_catalog/snotspil';out=r/'generated'
d=next(m for m in json.loads((src.parent/'monsters.json').read_text())['monsters'] if m['key']=='snotspil')
im=cl2_frames((src/'n.cl2').read_bytes(),d['width'])[0][0].point(list((src/'translation.trn').read_bytes()))
side=max(128,d['width'],im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height))
pal=(r/'source_assets/corpses/l1.pal').read_bytes();bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()));colors=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in bg]
lut=[0]+[min(range(1,256),key=lambda j:sum((colors[j][k]-pal[i*3+k])**2 for k in range(3))) for i in range(1,256)]
data=(out/'prop_frames.bin').read_bytes()[:60*4096];assert len(data)==60*4096
data+=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut).tobytes()+bytes(4096);(out/'prop_frames.bin').write_bytes(data);(out/'prop_frames.lz').write_bytes(indexed(data,4096));(out/'quest_banner.h').write_text('#pragma once\n#define PROP_SNOTSPIL_FRAME 60\n')
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(data)}',p.read_text()))
print('Native Snotspill talk frame appended; 62 prop frames')
