"""Full native Lachdanan death sequence; retain all established prop frame IDs."""
from pathlib import Path
import json,struct
from PIL import Image
from extract_assets import cl2_frames
from lz77 import indexed
R=Path(__file__).resolve().parents[1];g=R/'generated';s=R/'source_assets/objects'
base=int(__import__('re').search(r'PROP_FOUNTAIN_FRAME_LAST (\d+)',(g/'fountain_props.h').read_text())[1])+1
pal=struct.unpack('<256H',(g/'bg_palette.bin').read_bytes());native=(R/'source_assets/corpses/l1.pal').read_bytes();static=(g/'bg_static_slots.bin').read_bytes();slots=[i for i in range(1,256) if static[i//8]&(1<<(i%8))]
rgb=lambda c:tuple(((c>>shift)&31)*255//31 for shift in (0,5,10))
lut=[0]+[min(slots,key=lambda j:sum((rgb(pal[j])[k]-native[i*3+k])**2 for k in range(3))) for i in range(1,256)]
trn=list((s/'BHKA.TRN').read_bytes());frames=cl2_frames((s/'LachdananD.CL2').read_bytes(),160)[0];images=[]
for im in frames:
 im=im.point(trn);side=max(160,im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));images.append(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut).tobytes())
(g/'quest_objects71.lz').write_bytes(indexed(b''.join(images),4096))
(g/'quest_objects71.h').write_text(f'#pragma once\n#define PROP_QUEST71_BASE {base}\n#define PROP_LACHDANAN_DEATH_BASE {base}\n#define PROP_LACHDANAN_DEATH_FRAMES {len(images)}\n#define PROP_QUEST71_FRAMES {len(images)}\n#define PROP_QUEST71_FRAME_LAST {base+len(images)-1}\n')
(g/'quest-objects-stage71.json').write_text(json.dumps(dict(source='Monsters/Black/Blackd.CL2',translation='BHKA.TRN',frames=len(images),native_frames=len(frames),ticks_per_frame=2,ram_added=0),indent=2)+'\n')
print('Lachdanan native death:',len(images),'frames')
