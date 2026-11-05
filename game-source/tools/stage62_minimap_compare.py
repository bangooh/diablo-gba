"""Summarize bounded Stage61/62 same-scene native minimap measurements."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
out=root/'generated/stage62-validation'
def read(name):return json.loads((out/name/'report.json').read_text())
base,now=read('minimap-baseline'),read('minimap-native')
assert base['result']==now['result']=='PASS';assert base['fixtureWorld']==now['fixtureWorld']
report={'result':'PASS','baselineRomSha256':base['romSha256'],'romSha256':now['romSha256'],'nativeFixtureMatches':True,'physicalGba':False,'measurements':[],'limits':['Emulator hardware time, not physical GBA.','Rolling60hardware frames are approximately1.005seconds.','A bounded fixture comparison cannot establish minimum FPS for all campaigns.','Minor timer-phase changes can alter per-frame render scheduling; the same input schedule and final combat states are checked.']}
def add(name,a,b,af,bf,am,bm,over):
 assert not over
 report['measurements'].append({'scene':name,'baselineFps':a[af],'candidateFps':b[bf],'deltaFps':b[bf]-a[af],'baselineRollingMinimumFps':a[am],'candidateRollingMinimumFps':b[bm],'mapCommitOverruns':over})
for name in ['L1-native-fog-walk-on-0','L1-all-explored-walk-on-0']:
 a=next(s for s in base['samples'] if s['name']==name);b=next(s for s in now['samples'] if s['name']==name)
 assert a['logicalMoves']==b['logicalMoves']==25 and a['logicTicks']==b['logicTicks']==200
 add(name,a,b,'fps','fps','minRolling60HardwareFramesFps','minRolling60HardwareFramesFps',b['mapOverruns'])
 report['measurements'][-1].update(logicalMoves=b['logicalMoves'],mapRebuilds=b['mapChanges'],simulationHz=b['logicTicks']*now['hardwareHz']/b['hardwareFrames'])
base,now=read('firebolt-baseline'),read('firebolt-native')
assert base['result']==now['result']=='PASS' and base['fixture']==now['fixture']
assert now['romSha256']==report['romSha256']
for name in ['single-open','repeated-open']:
 a=next(s for s in base['cases'] if s['name']==name);b=next(s for s in now['cases'] if s['name']==name);assert a['casts']==b['casts'] and b['minimapOn']
 add('Firebolt-'+name,a,b,'fps','fps','minimumRolling60HardwareFramesFps','minimumRolling60HardwareFramesFps',b['mapOverruns']);report['measurements'][-1]['casts']=b['casts']
base,now=read('melee-baseline'),read('melee-native')
assert base['result']==now['result']=='PASS' and base['mapSha256']==now['mapSha256'] and base['arena']==now['arena']
assert now['romSha256']==report['romSha256']
a=next(s for s in base['cases'] if s['name']=='crowd12');b=next(s for s in now['cases'] if s['name']=='crowd12')
assert a['startActors']==b['startActors'] and a['finalActors']==b['finalActors'] and a['damage']==b['damage'] and a['attackStarts']==b['attackStarts'] and a['simulationTicks']==b['simulationTicks']
add('Twelve-enemy combat',a,b,'rendererFps','rendererFps','minimumRolling60HardwareFramesFps','minimumRolling60HardwareFramesFps',b['mapOverruns']);report['measurements'][-1].update(sameFinalActors=True,damage=b['damage'],attackStarts=b['attackStarts'],simulationHz=b['simulationTicksPerSecond'])
pixels=read('minimap-pixels-native');assert pixels['result']=='PASS' and pixels['romSha256']==report['romSha256']
report['nativePixelsAndUi']={'uiScenes':len(pixels['cases']),'uiChecks':sum(len(c['ui']) for c in pixels['cases']),'distantCacheAnchors':len(pixels['recenter'])}
report['minimum24FpsGoalMet']=all(m['candidateRollingMinimumFps']>=24 for m in report['measurements'])
(out/'minimap-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
