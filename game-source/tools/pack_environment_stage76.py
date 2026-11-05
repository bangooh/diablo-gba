"""Retail story books and restored flame trap CELs, using the static BG palette."""
from pathlib import Path
import sys,struct,json,re,hashlib
from PIL import Image
from pack_environment_stage61 import cel
from lz77 import indexed
from make_prop_dirty import damage_table
from stage53_dialogue_catalog import preprocess,body,enum,string,STRING,SOURCE
R=Path(__file__).resolve().parents[1];g=R/'generated';src=R/'source_assets/environment_stage76';src.mkdir(exist_ok=True)
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
 for name in ['BkSlbrnt','Flame1','Lever']:(src/(name+'.CEL')).write_bytes(read_mpq(a,'Objects\\'+name+'.CEL'))
pal=struct.unpack('<256H',(g/'bg_palette.bin').read_bytes());native=(R/'source_assets/corpses/l1.pal').read_bytes();static=(g/'bg_static_slots.bin').read_bytes();slots=[i for i in range(1,256) if static[i//8]&(1<<(i%8))]
rgb=lambda c:tuple(((c>>s)&31)*255//31 for s in (0,5,10))
lut=[0]+[min(slots,key=lambda j:sum((rgb(pal[j])[k]-native[i*3+k])**2 for k in range(3))) for i in range(1,256)]
base=int(re.search(r'PROP_QUEST71_FRAME_LAST (\d+)',(g/'quest_objects71.h').read_text())[1])+1
h=['#pragma once',f'#define PROP_ENV76_BASE {base}'];dirty=['#pragma once'];frames=[];audit=[]
for name,symbol,count in [('BkSlbrnt','STORY',6),('Flame1','FLAME',20),('Lever','FLAME_LEVER',2)]:
 raw=(src/(name+'.CEL')).read_bytes();assert struct.unpack_from('<I',raw)[0]>=count
 h += [f'#define PROP_{symbol}_BASE {base+len(frames)}',f'#define PROP_{symbol}_FRAMES {count}'];group=[]
 for f in range(1,count+1):
  im=cel(raw,f,96);im=im.resize((48,im.height//2),Image.Resampling.NEAREST);assert im.height<=64
  canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));group.append(canvas.point(lut).tobytes())
 if name=='Flame1':damage_table(dirty,'flame_frame_damage','PROP_FLAME_FRAMES',group)
 frames.extend(group);audit.append(dict(name=name,frames=count,sha256=hashlib.sha256(raw).hexdigest()))
h += [f'#define PROP_ENV76_FRAMES {len(frames)}',f'#define PROP_ENV76_FRAME_LAST {base+len(frames)-1}']
(g/'environment76.h').write_text('\n'.join(h)+'\n');(g/'environment76_dirty.h').write_text('\n'.join(dirty)+'\n');(g/'environment76.lz').write_bytes(indexed(b''.join(frames),4096))
# Same campaign story family at all three depths. Text and titles stay verbatim.
e=enum(preprocess(SOURCE/'enums.h'),'_speech_id');rows=re.findall(r'\{\s*((?:'+STRING+r'\s*)+),\s*(\d+),\s*(\d+),\s*(\w+)\s*\}',body(preprocess(SOURCE/'textdat.cpp'),'alltext[]'))
s=(R/'source_assets/item_catalog/objects.cpp').read_text();names=re.findall(r'"([^"]+)"',s[s.index('const char *const StoryBookName'):s.index('/** Specifies the speech')])[:9]
(g/'story_names76.h').write_text('#pragma once\nstatic const char *const story_names76[9]={'+','.join(json.dumps(n) for n in names)+'};\n')
prose=['#pragma once','static const struct {const char *title,*text;} story_prose76[9]={']
for n,title in enumerate(names):
 text=string(rows[e['TEXT_BOOK'+str(n//3+1)+str(n%3+1)]][0]).rstrip().removesuffix('|').rstrip();prose.append('{'+json.dumps(title)+','+json.dumps(text)+'},')
prose.append('};');(g/'story_prose76.h').write_text('\n'.join(prose)+'\n');(g/'environment-stage76-assets.json').write_text(json.dumps(audit,indent=2)+'\n')
print('Imported',len(frames),'native frames and 9 story passages')
