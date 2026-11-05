"""Native missing monster sounds, deduplicated by original MPQ path."""
import json,sys
from pathlib import Path
from pack_difficulty import retail,table
from pack_audio_stage66 import write
from extract_assets import read_mpq
import mpyq
r=Path(__file__).resolve().parents[1];src=r/'source_assets/monster_stage71';rs=json.loads((src/'catalog.json').read_text())['records'];rows=table(retail((r/'source_assets/monster_catalog/monstdat.cpp').read_text()),'MonsterData monsterdata[] = {')
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
lookup={c['source'].lower():c['effect_id'] for c in json.loads((r/'generated/monster-audio-report.json').read_text())}
lookup.update({c['mpq_path'].lower():c['effect_id'] for c in json.loads((r/'source_assets/monster_stage66/audio.json').read_text())['clips']});clips=[];ids=[]
for rec in rs:
 template=json.loads(rows[rec['native_id']][4]);states=[]
 for state in 'ahd':
  variants=[]
  for variant in (1,2):
   path=template.replace('%c',state).replace('%i',str(variant));key=path.lower()
   if key not in lookup:
    n=188+len(clips);stem='audio_monster71_'+str(n);source=src/(stem+'.wav');write(source,read_mpq(a,path));lookup[key]=n;clips.append(dict(symbol='asset_'+stem,effect_id=n,source=str(source.relative_to(r)),mpq_path=path))
   variants.append(lookup[key])
  states.append(variants)
 ids.append(states)
write(src/'audio.json',json.dumps({'clips':clips},indent=2)+'\n');write(r/'generated/monster71_audio.h','#pragma once\nstatic const uint8_t monster71_audio_ids[242][3][2]='+json.dumps(ids).replace('[','{').replace(']','}')+';\n');print('New sound clips',len(clips))
