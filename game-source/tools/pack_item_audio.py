"""Extract original ItemDropSnds/ItemInvSnds families for current equipment."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys,subprocess,json,re
from item_roster import IDS, binding, SOURCE
from extract_assets import read_mpq
import mpyq
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/item_audio';src.mkdir(exist_ok=True)
archive=mpyq.MPQArchive(sys.argv[1],listfile=False) if len(sys.argv)>1 else None
# Cursor -> ItemCAnimTbl -> ItemDropSnds/ItemInvSnds in Devilution items.cpp.
# Mace and sword share the original sword sound. All three bottles share POT.
families=['Gold','Flippot','Flipswor','Flipbow','Flipstaf','Flipcap','Fliplarm','Flipshld',
          'Invpot','Invsword','Invbow','Invstaf','Invcap','Invlarm','Invshiel']
original_count=len(families)
sound_files=dict(zip(['IS_GOLD','IS_FPOT','IS_FSWOR','IS_FBOW','IS_FSTAF','IS_FCAP','IS_FLARM','IS_FSHLD','IS_IPOT','IS_ISWORD','IS_IBOW','IS_ISTAF','IS_ICAP','IS_ILARM','IS_ISHIEL'],families))
sound_files.update({'IS_FBODY':'Flipbody','IS_IBODY':'Invbody','IS_FMUSH':'Flipmush','IS_IMUSH':'Invmush','IS_ISIGN':'Invsign','IS_FBLST':'Flipblst','IS_IBLST':'Invblst','IS_FANVL':'Flipanvl','IS_IANVL':'Invanvl','IS_FROCK':'Fliprock','IS_IROCK':'Invrock','IS_FAXE':'Flipaxe','IS_IAXE':'Invaxe','IS_FHARM':'Flipharm','IS_IHARM':'Invharm','IS_FRING':'Flipring','IS_IRING':'Invring','IS_FBOOK':'Flipbook','IS_IBOOK':'Invbook','IS_FSCRL':'Flipscrl','IS_ISCROL':'Invscrol'})
bindings=[binding(k) for k in range(len(IDS))]
for b in bindings:
 for key in ('drop_sound','pickup_sound'):
  name=sound_files[b[key]]
  if name not in families:families.append(name)
# Item extras follow the generated monster samples and two spell samples.
extra_base=max(d["effect_id"] for d in json.loads((out/"monster-audio-report.json").read_text()))+3
ids={name:(26+i if i<original_count else extra_base+i-original_count) for i,name in enumerate(families)}
mapping=[[ids[sound_files[b[key]]] for b in bindings] for key in ('drop_sound','pickup_sound')]
source=(SOURCE/'effects.cpp').read_text()
for name in families:assert 'Items\\\\'+name+'.wav' in source

header=['#pragma once'];samples=[];report={}
for name in families:
 path='Sfx\\Items\\'+name+'.wav';wav=src/(name+'.wav')
 if archive:wav.write_bytes(read_mpq(archive,path))
 pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
 symbol='audio_item_'+name.lower();(out/(symbol+'.lz')).write_bytes(guarded_pcm(pcm))
 header.append(f'extern const uint8_t asset_{symbol}[];');samples.append(f'{{asset_{symbol},{len(pcm)}}}')
 report[name]={'source':path,'samples':len(pcm),'seconds':len(pcm)/16384}
header+=['#define ITEM_AUDIO_SAMPLES '+','.join(samples[:original_count]),
 '#define ITEM_AUDIO_EXTRA_SAMPLES '+','.join(samples[original_count:]),
 '#define ITEM_AUDIO_EXTRA_COUNT '+str(len(families)-original_count),
 'static const uint8_t item_audio_ids[2][ITEM_COUNT]={',
 '{'+','.join(map(str,mapping[0]))+'},',
 '{'+','.join(map(str,mapping[1]))+'}};']
(out/'item_audio.h').write_text('\n'.join(header)+'\n')
(out/'item-audio-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'samples':len(samples),'pcm_bytes':sum(v['samples'] for v in report.values())}))
