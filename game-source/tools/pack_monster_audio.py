"""Use monsterdata.sndfile, not palette family, for original A/H/D variants."""
from pathlib import Path
import ast,json,re,sys,subprocess,mpyq
from extract_assets import read_mpq
from audio_pcm import guarded_pcm
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/monster_audio';src.mkdir(exist_ok=True)
# Reuse parser without executing the catalog packer's top-level extraction.
m=ast.parse((r/'tools/pack_cathedral_monsters.py').read_text());fn=next(n for n in m.body if isinstance(n,ast.FunctionDef) and n.name=='split')
exec(compile(ast.Module(body=[fn],type_ignores=[]),'catalog_split','exec'))
s=(r/'source_assets/monster_catalog/monstdat.cpp').read_text();s=s[s.index('MonsterData monsterdata[]'):];s=s[:s.index('\n};')]
rows=[split(l[l.index('{')+1:l.rfind('}')]) for l in s.splitlines() if re.match(r'\s*\{\s*\d+,',l)]
records=json.loads((r/'source_assets/monster_catalog/monsters.json').read_text())['monsters']
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
ids={};header=['#pragma once'];samples=[];mapping=[];report=[]
for d in records:
 template=json.loads(rows[d['source_id']][4]);result=[]
 for action,state in enumerate('ahd'):
  variants=[]
  for variant in (1,2):
   path='Sfx\\Monsters\\DiabloD.wav' if d['kind']==44 and state=='d' else template.replace('%c',state).replace('%i',str(variant));key=path.lower()
   if key not in ids:
    if d['kind']<7 and variant==1:ids[key]=4+d['kind']*3+action
    else:
     ids[key]=44+len(samples);symbol='audio_monster_'+str(ids[key]);wav=src/(symbol+'.wav');wav.write_bytes(read_mpq(a,path))
     pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
     (out/(symbol+'.lz')).write_bytes(guarded_pcm(pcm));header.append('extern const uint8_t asset_'+symbol+'[];');samples.append('{asset_'+symbol+','+str(len(pcm))+'}')
   variants.append(ids[key])
   report.append({'kind':d['kind'],'name':d['name'],'action':state,'variant':variant,'source':path,'effect_id':ids[key]})
  result.append('{'+','.join(map(str,variants))+'}')
 mapping.append('{'+','.join(result)+'}')
header.append('#define MONSTER_AUDIO_COUNT '+str(len(samples)))
header.append('#define MONSTER_AUDIO_SAMPLES '+','.join(samples))
header.append('static const uint8_t monster_audio_ids[][3][2]={'+','.join(mapping)+'};')
(out/'monster_audio.h').write_text('\n'.join(header)+'\n');(out/'monster-audio-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'kinds':len(records),'unique_sources':len(ids),'new_samples':len(samples)}))
