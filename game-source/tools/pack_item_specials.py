"""Retail monster class binding for equipment effects, pinned to monstdat.cpp."""
from pathlib import Path
import json,re,hashlib,subprocess
r=Path(__file__).resolve().parents[1];src=r/'source_assets/monster_catalog';data=json.loads((src/'monsters.json').read_text())['monsters']
# The older asset catalog stores raw source-row positions including both
# conditional branches. Bind by native name/graphic after retail preprocessing.
raw=(src/'monstdat.cpp').read_text().replace('#include "all.h"','')
raw=subprocess.check_output(['gcc','-E','-P','-x','c','-'],input=raw,text=True)
rows=[line for line in raw.splitlines() if re.search(r'^\s*\{.*\bMC_',line)]
values=[]
for d in data:
    matches=[line for line in rows if json.dumps({'Warlord of Blood':'Black Knight','Gharbad the Weak':'Night Clan','Zhar the Mad':'Counselor','Snotspill':'Dark One','Arch-Bishop Lazarus':'Advocate','Red Vex':'Hell Spawn','BlackJade':'Hell Spawn'}.get(d['name'],d['name'])) in line and json.dumps(d['graphic']) in line]
    assert len(matches)==1,(d['name'],len(matches))
    values.append(int(re.search(r'\bMC_(UNDEAD|DEMON|ANIMAL)\b',matches[0])[1]=='DEMON'))
for d in json.loads((src/'uniques.json').read_text())['monsters']:
    values.append(values[d['base_kind']])
for d in json.loads((r/'source_assets/monster_stage66/catalog.json').read_text())['records']:
    matches=[line for line in rows if json.dumps(d['name']) in line and json.dumps(d['graphic']) in line]
    assert len(matches)==1
    values.append(int('MC_DEMON' in matches[0]))
assert len(values)==64
(r/'generated/item_monster_class.h').write_text('#pragma once\nstatic const uint8_t item_demon_class[MONSTER_TYPES]={'+','.join(map(str,values))+'};\n')
(r/'generated/item-specials-source.json').write_text(json.dumps({'native_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','monster_class_source_sha256':hashlib.sha256((src/'monstdat.cpp').read_bytes()).hexdigest(),'demon_kinds':[d['kind'] for d,v in zip(data,values) if v],'combat_sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (r/'source_assets/item_catalog').glob('*.cpp')}},indent=2)+'\n')
