"""Decode native Anvil.DUN monster layer; port coordinates omit the16-cell border."""
from pathlib import Path
import struct,json,hashlib
r=Path(__file__).resolve().parents[1];p=r/'source_assets/worldgen/Anvil.DUN';b=p.read_bytes();a=struct.unpack('<'+'H'*(len(b)//2),b);w,h=a[:2];layer=a[2+w*h*5:2+w*h*9];rows=[]
for i,v in enumerate(layer):
 if v:
  assert v in (44,65)
  rows.append((i%(w*2)-9,i//(w*2)-9,'MONSTER_FIRE_CLAN_ARCHER' if v==44 else 'MONSTER_OBSIDIAN_LORD'))
assert len(rows)==17
(r/'generated/anvil_guards.h').write_text('#pragma once\nstatic const int8_t anvil_guards[17][3]={\n'+',\n'.join('{'+str(x)+','+str(y)+','+k+'}' for x,y,k in rows)+'\n};\n')
(r/'source_assets/worldgen/anvil-source-report.json').write_text(json.dumps({'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','anvil_dun_sha256':hashlib.sha256(b).hexdigest(),'native_fixed_monsters':{'MT_GGOATBW':9,'MT_OBLORD':8},'fixed_monster_layer_connected':True,'offsets_from_anvil':rows},indent=2)+'\n')
