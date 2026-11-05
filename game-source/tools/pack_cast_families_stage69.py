"""Native LM clips in an independent bounded sparse pool; FM/QM assets untouched."""
from pathlib import Path
import sys,struct,json,hashlib,re
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/stage69-casts';src.mkdir(exist_ok=True)
manifest=out/'cast-families-stage69-report.json'
sha=lambda b:hashlib.sha256(b).hexdigest()
if manifest.exists() and len(sys.argv)==1:
 d=json.loads(manifest.read_text())
 for n,h in d['outputs'].items():assert sha((out/n).read_bytes())==h,n
 print('PASS native LM pool and cast-family header hashes');sys.exit(0)
import numpy as np
from PIL import Image
from extract_assets import cl2_frames,read_mpq
from pack_gba_assets import tile_bytes
from refine_hero_pool import encode_nibbles
from lz77 import decompress,compress_actor
from pack_cast_frames import TOTAL
# Bounded32MiB compromise: retain release and end poses, max3 source ticks/pose.
SELECTED=((0,3,6,9,12,13,16,19),(0,3,6,9,11,12,15),(0,3,6,7,9,11))
a=None
if len(sys.argv)>1:
 import mpyq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
pal=(r/'source_assets/weapon_heroes/l1_1.pal').read_bytes();rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)]
clips={};seen={};pool=bytearray(struct.pack('<II',0,0x31465053));sources={};records=[]
for cls,(folder,letter,palette) in enumerate([('Warrior','W','obj'),('Rogue','R','rogue'),('Sorceror','S','sorcerer')]):
 colors=struct.unpack('<16H',decompress((out/(palette+'_palette.lz')).read_bytes())[:32]);colors=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in colors]
 lut=[0]+[min(range(1,16),key=lambda j:sum((colors[j][k]-rgb[i][k])**2 for k in range(3))) for i in range(1,256)]
 converted=[];external=[[] for _ in range(8)];external_refs=[[] for _ in range(8)]
 for armor in 'LMH':
  for weapon in 'NUSDBMHTA':
   prefix=letter+armor+weapon;name=prefix+'LM.CL2';p=src/name;mpq=f'PlrGFX\\{folder}\\{prefix}\\{name}'
   if a:p.write_bytes(read_mpq(a,mpq))
   raw=p.read_bytes();sources[name]={'mpq':mpq,'sha256':sha(raw)};groups=cl2_frames(raw,128 if cls==2 else 96);assert all(len(g)>=TOTAL[cls] for g in groups)
   frames=[]
   for group in groups:
    for index in SELECTED[cls]:
     im=group[index];canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height))
     frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
   converted.append((prefix,frames));clips[prefix]=[0]*len(frames)
   for state in ('fm','qm'):
    rawbase=(r/'source_assets/spells'/(prefix+state.upper()+'.CL2')).read_bytes();basegroups=cl2_frames(rawbase,128 if cls==2 else 96)
    report=json.loads((out/'weapon-heroes-report.json').read_text());entry=next(e for e in report['sets'] if e['prefix']==prefix)['states'][state]
    refs=struct.unpack_from('<'+'I'*(entry['frames']*8),(out/(entry['asset']+'.lz')).read_bytes(),8)
    for d,group in enumerate(basegroups):
     for j,index in enumerate(entry['selected_frames']):
      im=group[index];canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height))
      frame=tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4)
      external[d].append(np.frombuffer(frame,dtype=np.uint8));external_refs[d].append(refs[d*entry['frames']+j])
 for direction in range(8):
  candidates=[];refs=[];depths=[]
  for prefix,frames in converted:
   for i in range(direction*len(SELECTED[cls]),(direction+1)*len(SELECTED[cls])):
    frame=frames[i]
    if frame in seen:clips[prefix][i]=seen[frame];continue
    val=np.frombuffer(frame,dtype=np.uint8);body=encode_nibbles(frame);lz=compress_actor(frame);kind=4;depth=0
    if len(lz)<len(body):body=lz;kind=0
    best=len(body)
    ex=np.asarray(external[direction])^val;distance=np.count_nonzero(ex,axis=1)
    for j in np.argsort(distance)[:4]:
     enc=encode_nibbles(bytes(ex[j]));score=4+len(enc)+12
     if score<best:best=score;kind=5;depth=0;body=struct.pack('<I',external_refs[direction][j])+enc
    if refs:
     arr=np.asarray(candidates);delta=arr^val;dist=np.count_nonzero(delta,axis=1)
     for j in np.argsort(dist)[:4]:
      enc=encode_nibbles(bytes(delta[j]));score=4+len(enc)+4*(depths[j]+1)
      if score<best:best=score;kind=3;depth=depths[j]+1;body=struct.pack('<I',refs[j])+enc
    offset=len(pool);pool.extend(struct.pack('<BBH',kind,depth,len(body))+body);seen[frame]=offset;clips[prefix][i]=offset
    if depth<3:candidates.append(val);refs.append(offset);depths.append(depth)
  print('LM',cls,direction,'bytes',len(pool),flush=True)
 for prefix,frames in converted:records.append({'prefix':prefix,'frame_count':len(SELECTED[cls]),'decoded_sha256':sha(b''.join(frames))})
struct.pack_into('<I',pool,0,len(pool));outputs={'lightning_frame_pool.lz':bytes(pool)}
h=['#pragma once','extern const uint8_t asset_lightning_frame_pool[];']
for prefix,refs in clips.items():
 n='spell_'+prefix.lower()+'_lm';outputs[n+'.lz']=struct.pack('<II',0x33525053,len(refs))+struct.pack('<'+'I'*len(refs),*refs);h.append('extern const uint8_t asset_'+n+'[];')
h.append('static const struct {const uint8_t *data;unsigned frames;} lightning_hero_art[3][27]={'+','.join('{'+','.join('{asset_spell_'+(letter+a+w).lower()+'_lm,'+str(len(SELECTED[cls]))+'}' for a in 'LMH' for w in 'NUSDBMHTA')+'}' for cls,letter in enumerate('WRS'))+'};')
# Derive native cast family separately from damage element and cast sound.
raw=(r/'source_assets/item_catalog/spelldat.cpp').read_text();family={}
for m in re.finditer(r'\{\s*(SPL_\w+),\s*\d+,\s*(STYPE_\w+),\s*"([^"]+)"',raw):family.setdefault(m[3].upper(),{'STYPE_FIRE':0,'STYPE_LIGHTNING':2,'STYPE_MAGIC':1}[m[2]])
names={1:'FIREBOLT',2:'HEALING',3:'LIGHTNING',4:'FLASH',6:'FIRE WALL',7:'TOWN PORTAL',8:'STONE CURSE',9:'INFRAVISION',10:'PHASING',11:'MANA SHIELD',12:'FIREBALL',13:'GUARDIAN',14:'CHAIN LIGHTNING',15:'FLAME WAVE',18:'NOVA',20:'INFERNO',21:'GOLEM',23:'TELEPORT',24:'APOCALYPSE',29:'ELEMENTAL',30:'CHARGED BOLT',31:'HOLY BOLT',32:'RESURRECT',33:'TELEKINESIS',34:'HEAL OTHER',35:'BLOOD STAR',36:'BONE SPIRIT'}
h.append('static const uint8_t spell_cast_family[37]={'+','.join(str(family[names[i]]) if i in names else '1' for i in range(37))+'};')
h.append('static const uint8_t lightning_hero_cast_frame[3][20]={'+','.join('{'+','.join(str(max(j for j,x in enumerate(SELECTED[c]) if x<=min(t,TOTAL[c]-1))) for t in range(20))+'}' for c in range(3))+'};')
outputs['cast_families_stage69.h']=('\n'.join(h)+'\n').encode()
for n,b in outputs.items():(out/n).write_bytes(b)
manifest.write_text(json.dumps({'pool_bytes':len(pool),'asset_bytes':sum(map(len,outputs.values())),'sources':sources,'records':records,'family_by_name':family,'selection':SELECTED,'maximum_pose_ticks':3,'outputs':{n:sha(b) for n,b in outputs.items()}},indent=2)+'\n')
print('Native LM complete',len(pool),flush=True)
