"""Retail catalog completion. Original native rows, CL2 directions and TRNs.
Existing runtime IDs0..63 retained. New normal64..104, leaders105..172,
explicit authored minion aliases173..240. Disabled native rows remain dormant.
"""
import argparse,copy,json,re,struct,sys
from pathlib import Path
from pack_difficulty import retail,table,enum_ids,signature
from pack_monster_uniques import stats
from pack_monsters_stage66 import relocate,array
from pack_audio_stage66 import write,sha
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'generated';SRC=ROOT/'source_assets/monster_stage71'
BANKS=[(1,4),(2,6),(7,11),(8,12),(9,13),(10,14),(10,14)]
def records():
 text=(ROOT/'source_assets/monster_catalog/monstdat.cpp').read_text(); rows=table(retail(text),'MonsterData monsterdata[] = {');raw=table(text,'MonsterData monsterdata[] = {')
 avail=[{'MAT_NEVER':0,'MAT_ALWAYS':1,'MAT_RETAIL':2}[x] for x in re.findall(r'MAT_\w+',re.search(r'char MonstAvailTbl\[\] = \{(.*?)\};',retail(text),re.S)[1])]
 old=json.loads((ROOT/'source_assets/monster_catalog/monsters.json').read_text())['monsters']+json.loads((ROOT/'source_assets/monster_catalog/uniques.json').read_text())['monsters']+json.loads((ROOT/'source_assets/monster_stage66/catalog.json').read_text())['records']
 for r in old:r['native_id']=next(i for i,v in enumerate(rows) if signature(v)==signature(raw[r['source_id']]))
 present={r['native_id'] for r in old if r['kind']!=47};missing=[i for i,a in enumerate(avail) if a and i not in present]
 assert len(missing)==42
 families={'AI_ZOMBIE':0,'AI_FALLEN':2,'AI_SCAV':4,'AI_SKELBOW':0,'AI_SNEAK':2,'AI_BAT':0,'AI_GOATBOW':4,'AI_ACID':4,'AI_FAT':0,'AI_MAGMA':1,'AI_STORM':2,'AI_GARG':5,'AI_MEGA':4,'AI_SUCC':0,'AI_COUNSLR':1}
 new=[];extra=[]
 for sid in missing:
  v=rows[sid];arr=lambda t:list(map(int,t.strip('{} ').split(',')));name=json.loads(v[10]);key=re.sub(r'\W+','_',name.lower()).strip('_')+'_'+str(sid)
  r=dict(kind=241 if sid==105 else 64+len(new),native_id=sid,source_id=next(i for i,x in enumerate(raw) if signature(v)==signature(x)),key=key,name=name,width=int(v[0]),image_cost=int(v[1]),graphic=json.loads(v[2]),translation=None if v[7]=='NULL' else json.loads(v[7]),frames=arr(v[8]),rates=arr(v[9]),min_floor=15*int(v[11])//30+1,max_floor=15*int(v[12])//30+1,level=int(v[13]),hp_min=int(v[14]),hp_max=int(v[15]),ai=v[16],family=families[v[16]],intelligence=int(v[18]),hit=int(v[19]),impact=int(v[20]),damage_min=int(v[21]),damage_max=int(v[22]),armor=int(v[27]),xp=int(v[33]),undead=v[28]=='MC_UNDEAD',immune=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'IMMUNE_'+n in v[29]),resist=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'RESIST_'+n in v[29]))
  (extra if sid==105 else new).append(r)
 allr=old+new;ids=enum_ids((ROOT/'source_assets/item_catalog/enums.h').read_text());nmap={r['native_id']:r for r in allr+extra if not r.get('role') and r['kind'] not in (6,29,33,41,42,43,46,47,48)}
 # Quest bodies share geometry but must not replace ordinary source definitions.
 for r in allr:nmap.setdefault(r['native_id'],r)
 leaders=[];members=[];disabled=[]
 for u in table(retail(text),'UniqMonstStruct UniqMonst[] = {')[10:]:
  name=json.loads(u[1]);sid=ids[u[0]];floor=int(u[3])
  if not avail[sid] or not 1<=floor<=16:disabled.append(dict(name=name,native_id=sid,floor=floor));continue
  if any(r.get('role')=='leader' and r['name']==name for r in old):continue
  b=nmap[sid];r=copy.deepcopy(b);flags=int(u[10]);k=105+len(leaders)
  r.update(kind=k,key='unique71_'+str(k),name=name,role='leader',base_kind=b['kind'],native_type=u[0],unique_flags=flags,spawn_floor=floor,minion_kind=k+68,min_floor=floor,max_floor=floor,level=2*floor,hp_min=int(u[4]),hp_max=int(u[4]),ai=u[5],intelligence=int(u[6]),damage_min=int(u[7]),damage_max=int(u[8]),xp=b['xp']*2,hit=int(u[11]) if flags&4 else b['hit'],armor=int(u[11]) if flags&8 else b['armor'],immune=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'IMMUNE_'+n in u[9]),resist=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'RESIST_'+n in u[9]),translation='Monsters\\Monsters\\'+json.loads(u[2])+'.TRN',native_unique_fields=u)
  m=copy.deepcopy(b);m.update(kind=k+68,key=r['key']+'_minion',role='minion',base_kind=b['kind'],leader_kind=k,spawn_floor=floor,ai=u[5] if flags&2 else b['ai'],intelligence=int(u[6]),hp_min=b['hp_min']*2,hp_max=b['hp_max']*2)
  leaders.append(r);members.append(m)
 assert len(leaders)==68 and len(disabled)==15
 allr+=leaders+members+extra
 ais=sorted({r['ai'] for r in allr});meta=['#pragma once','enum {'+','.join('MONSTER_'+a+'='+str(i) for i,a in enumerate(ais))+'};']
 for r in allr:
  v=rows[r['native_id']];r['path_flags']=(1 if 'MFLAG_SEARCH' in v[17] else 0)|(2 if 'MFLAG_CAN_OPEN_DOOR' in v[17] else 0);r['demon']=v[28]=='MC_DEMON'
 for name,values in [('monster71_special_frames',[r['frames'][5] for r in allr]),('monster71_special_impact',[int(rows[r['native_id']][24]) for r in allr]),('monster71_unique_leashed',[int(bool(r.get('unique_flags',0)&2)) for r in allr]),('monster71_unique_pack',[int(bool(r.get('unique_flags',0)&1)) for r in allr]),('monster71_ai',[ais.index(r['ai']) for r in allr]),('monster71_base',[r.get('base_kind',r['kind']) for r in allr]),('monster71_normal',[int(bool(avail[r['native_id']]) and not r.get('role') and r['kind'] not in (6,29,33,41,42,43,46,47,48)) for r in allr]),('monster71_native',[r['native_id'] for r in allr]),('monster71_path',[r['path_flags'] for r in allr]),('monster71_demon',[int(r['demon']) for r in allr])]:meta.append('static const uint8_t '+name+'[]={'+','.join(map(str,values))+'};')
 write(OUT/'monster71_meta.h','\n'.join(meta)+'\n');write(SRC/'catalog.json',json.dumps(dict(records=allr,disabled_uniques=disabled,active_normal=sum(bool(x) for x in avail),active_uniques=72),indent=2)+'\n')
 write(OUT/'monster71_defs.inc',',\n'.join(stats(r) for r in allr[64:])+'\n');write(OUT/'monster71_undead.inc',','.join(str(int(r['undead'])) for r in allr[64:])+'\n')
 return allr

def import_art(mpq,rs,baseline):
 from PIL import Image
 import numpy as np,mpyq
 from extract_assets import read_mpq,cl2_frames
 from pack_gba_assets import obj_palette,tile_bytes,rgb15
 from pack_hot_monsters import raw_tiles
 from lz77 import compress,decompress
 from floor_codec import encode
 archive=mpyq.MPQArchive(str(mpq),listfile=False)
 native=read_mpq(archive,'Levels\\L1Data\\L1_1.PAL');write(SRC/'native.pal',native);colors=np.frombuffer(native,np.uint8).reshape(256,3);pal=[tuple(map(int,c)) for c in colors]
 bg=struct.unpack('<256H',decompress((OUT/'bg_palette.lz').read_bytes()));bg_rgb=np.array([[(v>>s)&31 for s in (0,5,10)] for v in bg],dtype=np.int32)*255//31;static=np.array(json.loads((OUT/'background-palette-review/audit.json').read_text())['palette']['static_slots']);static=static[static!=0]
 bg_map=np.array([0]+[int(static[np.argmin(((bg_rgb[static]-colors[i].astype(np.float64)*.80)**2).sum(axis=1))]) for i in range(1,256)],dtype=np.uint8)
 groups={};header=['#pragma once','#include <stdint.h>'];arts=[None]*len(rs);hits=[None]*len(rs);special=[None]*len(rs);finals={};final_pal={};deaths=[];death_pal=[];counts=[0]*len(rs);rates=[0]*len(rs);bases=[0]*len(rs);geometry=[]
 for r in rs:
  k=r['kind']
  if r.get('role')=='minion':continue
  key=r['graphic'].lower()
  if key not in groups:
   tag='m71_'+str(r['kind']);gs={};lut=None;assets={}
   for state in 'nwahds':
    if not r['frames']['nwahds'.index(state)]:continue
    data=read_mpq(archive,r['graphic'].replace('%c',state));write(SRC/(tag+'_'+state+'.cl2'),data);gs[state]=cl2_frames(data,r['width'])
   palette,lut=obj_palette([im for g in gs.values() for dr in g for im in dr],pal);weights=np.zeros(256,np.int64)
   for g in gs.values():
    for dr in g:
     for im in dr:weights+=np.bincount(np.frombuffer(im.tobytes(),np.uint8),minlength=256)
   shapes={}
   for state,g in gs.items():
    frames=[];original=[]
    for dr in g:
     for im in dr:
      side=max(128,r['width'],im.height);c=Image.new('P',(side,side));c.paste(im,((side-im.width)//2,side-im.height));c=c.resize((64,64),Image.Resampling.NEAREST);original.append(c.tobytes());frames.append(tile_bytes(c.point(lut),4))
    shapes[state]=original
    if state!='d':
     symbol='asset_'+tag+'_'+state;blob=raw_tiles(frames);write(OUT/(symbol[6:]+'.lz'),blob);header.append('extern const uint8_t '+symbol+'[];');assets[state]=(symbol,len(g[0]));geometry.append(dict(symbol=symbol,frames=len(frames),bytes=len(blob)))
   groups[key]=(assets,lut,weights,shapes)
   print('imported',tag,r['name'],flush=True)
  assets,lut,weights,shapes=groups[key]
  trn=bytes(range(256))
  chain=r.get('translation_chain') or ([r['translation']] if r.get('translation') else [])
  if r.get('role')=='leader' and r['kind']>=105:
   b=rs[r['base_kind']];chain=([b['translation']] if b.get('translation') else [])+[r['translation']]
  for trn_index,path in enumerate(chain):
   t=read_mpq(archive,path);write(SRC/(r['key']+'_'+str(trn_index)+'_'+str(len(chain))+'.trn'),t);trn=bytes(t[v] for v in trn)
  mapped=colors[np.frombuffer(trn,np.uint8)].astype(np.int64);fitted=[(0,0,0)]
  for slot in range(1,16):
   mask=np.array(lut)==slot;total=weights[mask].sum();fitted.append(tuple(map(int,np.floor((weights[mask,None]*mapped[mask]).sum(axis=0)/max(1,total)+.5))))
  bright=[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in fitted[1:]];symbol='asset_m71_pal_'+str(r['kind']);write(OUT/(symbol[6:]+'.lz'),compress(rgb15(fitted+bright)));header.append('extern const uint8_t '+symbol+'[];')
  bank,brightbank=BANKS[4 if rs[r['base_kind']]['family']==3 else 3] if r.get('role')=='leader' else BANKS[r['family']]
  arts[k]=('{'+','.join(assets[s][0] for s in 'nwa')+','+symbol+','+','.join(str(assets[s][1]) for s in 'nwa')+','+str(bank)+','+str(brightbank)+','+json.dumps(r['name'].upper())+'},')
  hits[k]=('{'+assets['h'][0]+','+str(assets['h'][1])+'},');special[k]=('{'+assets['s'][0]+','+str(assets['s'][1])+'},' if 's' in assets else '{0,0},')
  mapping=bytes([0]+[int(bg_map[trn[i]]) if trn[i] else int(static[np.argmin((bg_rgb[static]**2).sum(axis=1))]) for i in range(1,256)])
  n=len(shapes['d'])//8;bases[k]=len(deaths);counts[k]=n;rates[k]=r['rates'][4]+1
  deaths+=shapes['d'];death_pal += [mapping]*len(shapes['d']);finals[k]=[shapes['d'][d*n+n-1] for d in range(8)];final_pal[k]=[mapping]*8
 # Build static aliases separately to preserve every kind*8 index.
 base_shapes=[];base_pal=[]
 for r in rs:
  k=r['kind'];b=r['base_kind'] if r.get('role')=='minion' else k
  if b!=k:arts[k]=arts[b];hits[k]=hits[b];special[k]=special[b];counts[k]=counts[b];rates[k]=rates[b];bases[k]=bases[b]
  base_shapes+=finals[b];base_pal+=final_pal[b]
 blob,report=encode(base_shapes+deaths,base_pal+death_pal);write(OUT/'monster71_corpses.rle',blob)
 for name,text in [('monsters_stage71.h','\n'.join(header)+'\n'),('monster71_art.inc','\n'.join(arts)+'\n'),('monster71_hit.inc','\n'.join(hits)+'\n'),('monster71_special.inc','\n'.join(special)+'\n')]:write(OUT/name,text)
 write(SRC/'death.json',json.dumps(dict(bases=bases,counts=counts,rates=rates,geometry=geometry),indent=2)+'\n')
 for name in ('floor_decals.rle','floor_decals.h'):write(SRC/('baseline-'+name),(baseline/'generated'/name).read_bytes())

def assemble(rs):
 old=(SRC/'baseline-floor_decals.rle').read_bytes();new=(OUT/'monster71_corpses.rle').read_bytes();h=(SRC/'baseline-floor_decals.h').read_text();item=int(re.search(r'FLOOR_ITEM_BASE\s*=\s*(\d+)',h)[1]);bright=int(re.search(r'FLOOR_BRIGHT_BASE\s*=\s*(\d+)',h)[1]);nc=struct.unpack_from('<I',new)[0];oc=struct.unpack_from('<I',old)[0];count=nc+oc-item
 b,p=relocate(new,4+4*count);start=4+4*count+len(b);pad=bytes(-start%4);ground=(OUT/'ground_items66.rle').read_bytes();assert struct.unpack_from('<I',ground)[0]==oc-item;c,q=relocate(ground,start+len(pad));p+=q;blob=struct.pack('<I',count)+struct.pack('<'+'I'*count,*p)+b+pad+c
 # Append only the current ground-item stream, excluding superseded corpses.
 write(OUT/'floor_decals.rle',blob);write(OUT/'floor_decals.h',f'#pragma once\nextern const uint8_t asset_floor_decals[];\nenum {{FLOOR_ITEM_BASE={nc},FLOOR_BRIGHT_BASE={nc+bright-item}}};\n')
 d=json.loads((SRC/'death.json').read_text());n=len(rs)*8
 write(OUT/'monster_death.h','#pragma once\n#define MONSTER_DEATH_TYPES '+str(len(rs))+'\nstatic const uint16_t monster_death_base[]={'+','.join(str(n+x) for x in d['bases'])+'};\nstatic const uint8_t monster_death_frames[]={'+','.join(map(str,d['counts']))+'};\nstatic const uint8_t monster_death_ticks[]={'+','.join(map(str,d['rates']))+'};\n')
 for inc in ('defs','undead'):
  p=OUT/'monster_defs.inc';s=p.read_text();needle='#include "monster66_'+inc+'.inc"';s=s.replace(needle,needle+'\n,\n#include "monster71_'+inc+'.inc"') if '#include "monster71_'+inc+'.inc"' not in s else s;write(p,s)
 print('Stage71 catalog',len(rs),'kinds; corpse bytes',len(blob),flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--mpq',type=Path);p.add_argument('--baseline',type=Path);a=p.parse_args();rs=records()
 if a.mpq:import_art(a.mpq,rs,a.baseline)
 assemble(rs)
