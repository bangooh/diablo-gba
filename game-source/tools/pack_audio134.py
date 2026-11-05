"""Extract class death voices and 8-kHz gold from the user's Diablo MPQ.
Generated streams are retained; the MPQ is unnecessary for normal ROM builds.
"""
import sys, subprocess, hashlib, json
from pathlib import Path
# Use the retained MPQ reader; dependencies mpyq and dclimplode.
from extract_mpq134 import read_mpq
import mpyq
root=Path(__file__).resolve().parents[1]; out=root/'generated'
archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
clips=[('hero_death_warrior',r'Sfx\Warrior\Warior71.wav',6144),('hero_death_rogue',r'Sfx\Rogue\Rogue71.wav',6144),('hero_death_sorcerer',r'Sfx\Sorceror\Mage71.wav',6144),('item_gold',r'Sfx\Items\Gold.wav',8000)]
header=['#pragma once','#include <stdint.h>']; samples=[];report=[]
for name,source,rate in clips:
 wav=read_mpq(archive,source)
 pcm=subprocess.run(['ffmpeg','-v','error','-i','pipe:0','-ac','1','-ar',str(rate),'-af','volume=0.85','-f','s8','pipe:1'],input=wav,stdout=subprocess.PIPE,check=True).stdout
 payload=pcm+b'\0'*(1024+(-len(pcm)%4));(out/('audio_'+name+'.lz')).write_bytes(payload)
 if name.startswith('hero'):
  header.append('extern const uint8_t asset_audio_'+name+'[];');samples.append('{asset_audio_'+name+','+str(len(pcm))+'}')
 report.append({'source':source,'rate':rate,'samples':len(pcm),'source_sha256':hashlib.sha256(wav).hexdigest(),'asset_sha256':hashlib.sha256(payload).hexdigest()})
header.append('#define HERO_DEATH_AUDIO_SAMPLES '+','.join(samples))
(out/'hero_death_audio.h').write_text('\n'.join(header)+'\n');(out/'audio134-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
