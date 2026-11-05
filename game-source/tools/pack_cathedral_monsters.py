"""Source-derived supported Cathedral variants, native CL2/TRN, indexed LZ frames.
Usage: pack_cathedral_monsters.py DIABDAT.MPQ [Devilution source root]
"""
from pathlib import Path
import sys,re,json,struct,shutil
import mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import obj_palette,tile_bytes,rgb15
from lz77 import indexed,compress
from legacy_monster_palette_style import style_palette
r=Path(__file__).resolve().parents[1];src=r/'source_assets/monster_catalog';src.mkdir(exist_ok=True);out=r/'generated'
if len(sys.argv)>2 and sys.argv[2] not in ("--reuse", "--catalog-only"):
 d=Path(sys.argv[2]);shutil.copyfile(d/'Source/monstdat.cpp',src/'monstdat.cpp');shutil.copyfile(d/'LICENSE.md',src/'LICENSE.md')
def split(line):
 fields=[];start=0;depth=0;quoted=False;escape=False
 for i,c in enumerate(line):
  if quoted:
   if escape:escape=False
   elif c=='\\':escape=True
   elif c=='"':quoted=False
  elif c=='"':quoted=True
  elif c=='{':depth+=1
  elif c=='}':depth-=1
  elif c==',' and depth==0:fields.append(line[start:i].strip());start=i+1
 fields.append(line[start:].strip());return fields
# IDs are retail table row IDs, so collect all widths, stopping before other tables.
text=(src/'monstdat.cpp').read_text();text=text[text.index('MonsterData monsterdata[]'):];text=text[:text.index('\n};')]
rows=[split(line[line.index('{')+1:line.rfind('}')]) for line in text.splitlines() if re.match(r'\s*\{\s*\d+,',line)]
ids=[0,8,4,12,16,24, next(i for i,v in enumerate(rows) if 'The Butcher' in v[10]),1,2,9,10,11,25,26,27]
families=[0,1,2,3,4,5,6,0,0,1,1,1,5,5,5]
keys=['zombie','skeleton','fallen_spear','fallen_sword','scavenger','captain','butcher','ghoul','rotting_carcass','corpse_axe','burning_dead','horror','corpse_captain','burning_captain','horror_captain']
ids += [36,37,38,39,101,102,103,104]
families += [1]*4+[5]*4
keys += ['flesh_clan','stone_clan','fire_clan','night_clan','black_knight','doom_guard','steel_lord','blood_knight']
ids += [next(i for i,v in enumerate(rows) if json.loads(v[10])==name) for name in ['Horned Demon','Mud Runner','Frost Charger','Obsidian Lord']]
families += [4]*4
keys += ['horned_demon','mud_runner','frost_charger','obsidian_lord']
ids += [next(i for i,v in enumerate(rows) if json.loads(v[10])=='Fire Clan' and v[16]=='AI_GOATBOW')]
families += [2]
keys += ['fire_clan_archer']
ids += [next(i for i,v in enumerate(rows) if json.loads(v[10])=='Illusion Weaver')]
families += [2]
keys += ['illusion_weaver']
ids += [7,7,15,next(i for i,v in enumerate(rows) if json.loads(v[10])=='Overlord')]
families += [4,2,3,0]
keys += ['snotspil','blue_fallen_spear','blue_fallen_sword','overlord']
ids += [next(i for i,v in enumerate(rows) if json.loads(v[10])=='Skeleton King'),21,23,22]
families += [4,0,2,3]
keys += ['skeleton_king','corpse_bow','horror_bow','burning_bow']
ids += [44,14,5]
families += [4,3,2]
keys += ['flesh_clan_archer','carver_sword','devil_kin_spear']
ids += [next(i for i,v in enumerate(rows) if json.loads(v[10])==name) for name in ['Advocate','Advocate','Hell Spawn','Hell Spawn','The Dark Lord','Hell Spawn']]
families += [1,4,2,3,4,0]
keys += ['advocate','lazarus','red_vex','black_jade','diablo','hell_spawn']
ids += [39,next(i for i,v in enumerate(rows) if json.loads(v[10])=='Counselor')]
families += [1,4]
keys += ['gharbad','zhar']
ids += [101,next(i for i,v in enumerate(rows) if json.loads(v[10])=='Unseen')]
families += [1,3]
keys += ['warlord','unseen']
catalog_only = '--catalog-only' in sys.argv
if not catalog_only:
 a=mpyq.MPQArchive(sys.argv[1],listfile=False);raw=read_mpq(a,'Levels\\L1Data\\L1_1.PAL');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
records=[];header=['#pragma once'];art=[];hit=[];stats=[]
for kind,(sid,family,key) in enumerate(zip(ids,families,keys)):
 v=rows[sid];assert len(v)==34,(sid,len(v))
 st=lambda x:json.loads(x)
 arr=lambda x:list(map(int,x.strip('{} ').split(',')))
 d={'width':int(v[0]),'kind':kind,'source_id':sid,'key':key,'family':family,'name':st(v[10]),'graphic':st(v[2]),'translation':None if v[7]=='NULL' else st(v[7]),'image_cost':int(v[1]),'frames':arr(v[8]),'rates':arr(v[9]),'min_floor':15*int(v[11])//30+1,'max_floor':15*int(v[12])//30+1,'level':int(v[13]),'hp_min':int(v[14]),'hp_max':int(v[15]),'ai':v[16],'intelligence':int(v[18]),'hit':int(v[19]),'impact':int(v[20]),'damage_min':int(v[21]),'damage_max':int(v[22]),'armor':int(v[27]),'xp':int(v[33]),'immune':sum(1<<i for i,n in enumerate(['MAGIC','FIRE','LIGHTNING']) if 'IMMUNE_'+n in v[29]),'resist':sum(1<<i for i,n in enumerate(['MAGIC','FIRE','LIGHTNING']) if 'RESIST_'+n in v[29])}
 if key=='warlord':d.update(name='Warlord of Blood',translation='Monsters\\Monsters\\GENERAL.TRN',hp_min=850,hp_max=850,damage_min=35,damage_max=50,intelligence=3,immune=7,resist=0)
 if key in ('gharbad','zhar'):
  uq={'gharbad':('Gharbad the Weak','BSDB',120,8,16,4,0),'zhar':('Zhar the Mad','GENERAL',360,16,40,1,6)}[key]
  d.update(name=uq[0],translation='Monsters\\Monsters\\'+uq[1]+'.TRN',hp_min=uq[2],hp_max=uq[2],damage_min=uq[3],damage_max=uq[4],intelligence=3,immune=uq[5],resist=uq[6])
 if key in ('lazarus','red_vex','black_jade'):
  unique={'lazarus':('Arch-Bishop Lazarus','GENERAL',600,1,6),'red_vex':('Red Vex','REDV',400,1,2),'black_jade':('BlackJade','BLKJD',400,1,4)}[key]
  d.update(name=unique[0],translation='Monsters\\Monsters\\'+unique[1]+'.TRN',hp_min=unique[2],hp_max=unique[2],damage_min=30,damage_max=50,intelligence=3,immune=unique[3],resist=unique[4])
 if key=='skeleton_king':d.update(translation='Monsters\\Monsters\\GENRL.TRN',level=14,hp_min=240,hp_max=240,xp=1140)
 if key=='snotspil':d.update(name='Snotspill',translation='Monsters\\Monsters\\BNG.TRN',level=8,hp_min=220,hp_max=220,damage_min=10,damage_max=18,resist=4)
 if kind==6:d.update(level=6,hp_min=220,hp_max=220,xp=1420,immune=0,resist=6)
 d['undead']=v[28]=='MC_UNDEAD';records.append(d)
 stats.append('{'+','.join(map(str,[family,d['min_floor'],d['max_floor'],d['level'],d['hp_min'],d['hp_max'],d['intelligence'],d['hit'],d['impact'],d['damage_min'],d['damage_max'],d['armor'],d['immune'],d['resist'],d['frames'][1],d['frames'][3],d['image_cost'],d['xp'],d['frames'][2]]))+'}')
 if catalog_only or kind<7:continue
 if '--reuse' in sys.argv and key not in ('skeleton_king','carver_sword','devil_kin_spear','advocate','lazarus','diablo','zhar') and (out/(key+'_h.lz')).exists() and (src/key/'d.cl2').exists():
  bank,bright={0:(1,4),1:(2,6),2:(7,11),3:(8,12),6:(10,14),4:(9,13),5:(10,14)}[family]
  header += ['extern const uint8_t asset_'+key+'_'+state+'[];' for state in ['n','w','a','h','palette']]
  art.append('{'+','.join('asset_'+key+'_'+state for state in ['n','w','a','palette'])+','+','.join(map(str,d['frames'][:3]+[bank,bright]))+',"'+d['name'].upper()+'"},')
  hit.append('{asset_'+key+'_h,'+str(d['frames'][3])+'},')
  continue
 folder=src/key;folder.mkdir(exist_ok=True);translation=read_mpq(a,d['translation']) if d['translation'] else bytes(range(256));(folder/'translation.trn').write_bytes(translation)
 groups={}
 for state in ('nwahds' if key in ('skeleton_king','carver_sword','devil_kin_spear','advocate','lazarus','diablo','zhar') else 'nwahd'):
  data=read_mpq(a,d['graphic'].replace('%c',state));(folder/(state+'.cl2')).write_bytes(data);groups[state]=[[im.point(list(translation)) for im in direction] for direction in cl2_frames(data,d['width'])]
 palette,lut=obj_palette([im for group in groups.values() for direction in group for im in direction],pal)
 (out/(key+'_palette.lz')).write_bytes(compress(style_palette(key,rgb15(palette+[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in palette[1:]]))))
 header.append('extern const uint8_t asset_'+key+'_palette[];')
 for state,group in groups.items():
  if state=="d":continue # corpse BG decals consume the source final death frames
  frames=[];count=len(group[0]);assert len(group)==8 and all(len(di)==count for di in group)
  for direction in group:
   for im in direction:
    side=max(128,d['width'],im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
  (out/(key+'_'+state+'.lz')).write_bytes(indexed(b''.join(frames),2048));header.append(f'extern const uint8_t asset_{key}_{state}[];')
 bank,bright={0:(1,4),1:(2,6),2:(7,11),3:(8,12),6:(10,14),4:(9,13),5:(10,14)}[family]
 art.append('{'+','.join('asset_'+key+'_'+s for s in ['n','w','a','palette'])+','+','.join(map(str,d['frames'][:3]+[bank,bright]))+',"'+d['name'].upper()+'"},')
 hit.append('{asset_'+key+'_h,'+str(d['frames'][3])+'},')
if catalog_only:
 (src/'monsters.json').write_text(json.dumps({'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','monsters':records},indent=2)+'\n')
 (out/'monster_defs.inc').write_text('const MonsterDef monster_defs[MONSTER_TYPES]={\n'+',\n'.join(stats)+'\n};\nconst uint8_t monster_undead[MONSTER_TYPES]={'+','.join(str(int(d['undead'])) for d in records)+'};\n')
 print('Generated',len(records),'source-only base monster records');sys.exit(0)
(out/'cathedral_monsters.h').write_text('\n'.join(header)+'\n')
(out/'cathedral_monster_art.inc').write_text('\n'.join(art)+'\n');(out/'cathedral_monster_hit.inc').write_text('\n'.join(hit)+'\n')
(out/'monster_defs.inc').write_text('const MonsterDef monster_defs[MONSTER_TYPES]={\n'+',\n'.join(stats)+'\n};\nconst uint8_t monster_undead[MONSTER_TYPES]={'+','.join(str(int(d['undead'])) for d in records)+'};\n')
(src/'monsters.json').write_text(json.dumps({'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','monsters':records},indent=2)+'\n')
# Keep the immutable body catalog at50 entries. Stage26 explicit leader/member
# kinds append their gameplay definitions without duplicating body art/audio.
from pack_monster_uniques import generate as generate_unique_data
generate_unique_data(r)
print(json.dumps({'types':len(records),'new_variants':keys[7:],'source_ids':ids}))
