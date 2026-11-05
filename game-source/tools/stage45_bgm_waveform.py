#!/usr/bin/env python3
"""Compare captured mGBA PCM with native sources; diagnostic, not underrun proof."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('runtime',type=Path);p.add_argument('rom',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];r=json.loads((a.runtime/'report.json').read_text())
raw=np.frombuffer((root/'generated/audio_music_town.lz').read_bytes(),dtype=np.int8).astype(float)
music=np.trunc((raw*130+np.where(raw<0,-100,100))/200)
fx=np.frombuffer((root/'generated/audio_monster_129.lz').read_bytes(),dtype=np.int8).astype(int)
effects=np.clip(fx*2,-128,127)
report={'romSha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'note':'Time alignment fit accounts for mGBA output resampling and short sample-timer handoffs. Correlation is supporting waveform evidence, not proof of no FIFO underruns. Native host fixture checks exact streaming samples.'}
for label,filename,meta,limit in [('music','bgm-runtime.s16le',r,60000),('mixed','bgm-mixed-runtime.s16le',r['mixed'],0)]:
 y=np.fromfile(a.runtime/filename,dtype='<i2').reshape(-1,2)[:,0]/192
 if limit:y=y[:limit]
 rate=(meta['endSample']-meta['startSample'])/(meta['pcmBytes']/4);best=None
 for lag in np.arange(-32,33,.125):
  pos=np.floor(meta['startSample']+np.arange(len(y))*rate+lag).astype(int)
  z=music[pos] if label=='music' else music[pos-1]+effects[pos-meta['voiceStart']]
  error=float(np.mean((y-z)**2))
  if best is None or error<best['meanSquareError']:
   best={'meanSquareError':error,'fittedNativeSampleLag':float(lag),'equalSampleFraction':float(np.mean(y==z)),'correlation':float(np.corrcoef(y,z)[0,1]),'observedRms':float(np.std(y)),'expectedRms':float(np.std(z)),'captureSamplesAnalyzed':len(y),'nativeSamplesPerCaptureSample':rate}
 report[label]=best
assert report['music']['correlation']>.99,report
(a.runtime.parent/'waveform-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
