"""Import Diablo (not HELLFIRE/SPAWN) AllItemsList exactly; build compact ROM tables.
Run --extract to compile the bundled original initializer with its original enums.
Normal builds regenerate from the checked-in JSON, requiring only Python.
"""
from pathlib import Path
import argparse, subprocess, tempfile, csv, json, hashlib, re
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'source_assets/item_catalog';OUT=ROOT/'generated'
fields='iRnd iClass iLoc iCurs itype iItemId iName iSName iMinMLvl iDurability iMinDam iMaxDam iMinAC iMaxAC iMinStr iMinMag iMinDex iFlags iMiscId iSpell iUsable iValue iMaxValue'.split()
strings={'iName','iSName'}
p=argparse.ArgumentParser();p.add_argument('--extract',action='store_true');args=p.parse_args()
if args.extract:
 source=(SRC/'itemdat.cpp').read_text();table=source[source.index('ItemDataStruct AllItemsList[]'):source.index('/** Contains the data related to each item prefix.')]
 program='#include <stdio.h>\n#include <stddef.h>\n#define TRUE 1\n#define FALSE 0\n#define PAL16_BLUE 176\n#define PAL16_YELLOW 192\n#define PAL16_RED 224\n#include "'+str(SRC/'enums.h')+'"\ntypedef struct {'+';'.join(('const char *' if f in strings else 'long long ')+f for f in fields)+';} ItemDataStruct;\n'+table+'\nint main(){for(auto &r:AllItemsList){'
 for i,f in enumerate(fields):
  program+='printf("'+('%s' if f in strings else '%lld')+('\\n' if i==len(fields)-1 else '\\t')+'",'+('r.'+f+'?r.'+f+':"<NULL>"' if f in strings else 'r.'+f)+');'
 program+='}}'
 with tempfile.TemporaryDirectory() as tmp:
  cpp=Path(tmp)/'export.cpp';exe=Path(tmp)/'export';cpp.write_text(program);subprocess.run(['g++','-std=c++11',str(cpp),'-o',str(exe)],check=True)
  rows=list(csv.reader(subprocess.check_output([str(exe)],text=True).splitlines(),delimiter='\t'))
 assert rows[-1][6]=='<NULL>' and len(rows)==157
 records=[]
 for i,row in enumerate(rows[:-1]):
  assert len(row)==len(fields)
  d={'source_id':i}
  for f,v in zip(fields,row):d[f]=(None if v=='<NULL>' else v) if f in strings else int(v)
  records.append(d)
 (SRC/'base_items.json').write_text(json.dumps({'revision':'9f01757243412bb154d02d9af65c804159e6e0ea','variant':'Diablo retail; HELLFIRE and SPAWN undefined','source_sha256':{n:hashlib.sha256((SRC/n).read_bytes()).hexdigest() for n in ['itemdat.cpp','enums.h']},'items':records},indent=2)+'\n')
data=json.loads((SRC/'base_items.json').read_text());rows=data['items'];assert len(rows)==156
for n,h in data['source_sha256'].items():assert hashlib.sha256((SRC/n).read_bytes()).hexdigest()==h
for i,d in enumerate(rows):
 assert d['source_id']==i
 for f in fields:
  if f not in strings:assert isinstance(d[f],int)
from item_roster import IDS, GEAR_IDS, binding, symbol, category
ids=IDS
(ROOT/'generated/item_kinds.h').write_text('#pragma once\nenum { '+','.join(symbol(k) for k in range(len(ids)))+',ITEM_COUNT };\n')
# Compile every original base row into ROM, including dormant quest/spell rows.
header='''#ifndef ITEM_CATALOG_H
#define ITEM_CATALOG_H
#include <stdint.h>
#define BASE_ITEM_COUNT 156
/* Native Diablo IDs, independent of stable prototype inventory IDs. */
typedef struct {
 const char *name,*short_name;
 uint32_t effects;
 uint16_t value,max_value,cursor;
 uint8_t drop_rate,item_class;
 int8_t location,type;
 uint8_t unique_base,min_level,durability,min_damage,max_damage,min_ac,max_ac;
 uint8_t strength,magic,dexterity,misc,spell,usable;
} BaseItemDef;
extern const BaseItemDef base_item_defs[BASE_ITEM_COUNT];
const BaseItemDef *item_base(unsigned kind);
#endif
'''
(ROOT/'src/item_catalog.h').write_text(header)
order=['iName','iSName','iFlags','iValue','iMaxValue','iCurs','iRnd','iClass','iLoc','itype','iItemId','iMinMLvl','iDurability','iMinDam','iMaxDam','iMinAC','iMaxAC','iMinStr','iMinMag','iMinDex','iMiscId','iSpell','iUsable']
body='#include "../src/item_catalog.h"\nconst BaseItemDef base_item_defs[BASE_ITEM_COUNT]={\n'
for d in rows:
 vals=[]
 for f in order:
  v=d[f]
  if f in strings:vals.append('0' if v is None else json.dumps(v))
  else:
   lo,hi=(-128,127) if f in ['iLoc','itype'] else (0,0xffffffff) if f=='iFlags' else (0,65535) if f in ['iValue','iMaxValue','iCurs'] else (0,255)
   assert lo<=v<=hi,(d['source_id'],f,v)
   vals.append(str(v)+'u' if f=='iFlags' else str(v))
 body+=' {'+','.join(vals)+'}, /* '+str(d['source_id'])+' */\n'
body+='};\nstatic const uint8_t item_base_ids[ITEM_COUNT]={'+','.join(map(str,ids))+'};\nconst BaseItemDef *item_base(unsigned kind){return kind<ITEM_COUNT?&base_item_defs[item_base_ids[kind]]:0;}\n'
(OUT/'item_catalog.inc').write_text(body)
# All ordinary weapons/armour/jewellery; no quest rewards in random drops.
defs='const ItemDef item_defs[ITEM_COUNT]={\n'
for k,i in enumerate(ids):
 d=rows[i];art=binding(k);loc=d['iLoc']
 slot={1:5 if d['itype']==5 else 4,2:4,3:6,4:0,5:1,6:3}.get(loc,255)
 flags=(1 if d['itype']==3 else 0)|(2 if loc==2 else 0)
 label=d['iName'].upper().replace("'",'')
 if k<4:label=['GOLD','HEALTH POTION','MANA POTION','POTION OF REJUVENATION'][k]
 if k in (21,22):label=['BOOK OF FIREBOLT','BOOK OF HEALING'][k-21]
 vals=[slot,art['width'],art['height'],d['iMinMLvl'],0 if k in (0,22) else d['iRnd'],d['iDurability'],d['iMinDam'],d['iMaxDam'],d['iMinAC'],d['iMaxAC'],d['iMinStr'],d['iMinMag'],d['iMinDex'],flags]
 if k in (21,22):vals[11]=15 if k==21 else 17
 defs+=' {'+json.dumps(label)+','+','.join(map(str,vals))+'},\n'
defs+='};\n';(OUT/'item_defs.inc').write_text(defs)
report={'base_rows':len(rows),'named_items':sum(d['iName'] is not None for d in rows),'reserved_rows':sum(d['iName'] is None for d in rows),'sentinel_excluded':True,'hellfire_included':False,'fields_per_row':23,'runtime_kinds':len(ids),'ordinary_equipment':len(GEAR_IDS),'kind_to_source_id':ids,'generated_table_sha256':hashlib.sha256(body.encode()).hexdigest(),'source':data['source_sha256'],'bindings':[binding(k) for k in range(len(ids))],'classification':[{'source_id':d['source_id'],'name':d['iName'],'category':category(d),'runtime_kinds':[k for k,i in enumerate(ids) if i==d['source_id']]} for d in rows],'limitations':['Runtime supports 100 kinds, 178 affixes and 90 uniques, including six fixed-spell staves, Cleaver, Empyrean Band, Griswold Edge, Arkaine Valor, Optic Amulet, Veil of Steel, Harlequin Crest, Undead Crown and Ring of Truth.','Identify, recharge and infravision are connected; infravision rendering remains simplified.','Poisoned Water has a separate native map, 38 fixed guards, foul/clean palettes and Pepin Ring reward. King Leoric has a separate native map, gates, boss and Crown reward. Quest paths are single-player only. Anvil fixed guards and physical archer attacks are connected; charge/Hidden AI, full Lachdanan animation/voice and hardware validation remain.']}
(OUT/'item-catalog-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'Diablo catalog: {len(rows)} source rows, {len(ids)} runtime kinds, {len(GEAR_IDS)} ordinary equipment bindings')
