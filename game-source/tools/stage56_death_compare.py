#!/usr/bin/env python3
"""Compare the same native eight-kill fixtures; no emulator work or ROM writes."""
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('output',type=Path);p.add_argument('--scope',choices=['isolated','combined'],default='isolated');args=p.parse_args()
a=json.loads(args.baseline.read_text());b=json.loads(args.candidate.read_text())
assert a['result']==b['result']=='PASS'
assert len(a['cases'])==len(b['cases'])==8
rows=[]
for x,y in zip(a['cases'],b['cases']):
 assert (x['level'],x['kind'])==(y['level'],y['kind'])
 assert x['fixture']==y['fixture'], 'paired world/input fixture differs'
 assert x['gameplay']==y['gameplay'], 'death/loot/AI RNG outcome differs'
 assert x['badFrames']==y['badFrames']==0
 assert x['commitTiming']['overruns']==y['commitTiming']['overruns']==0
 ap=x['performance'];bp=y['performance']
 rows.append({'level':x['level'],'kind':x['kind'],'fixtureEqual':True,'gameplayEqual':True,'before':ap,'after':bp,
  'renderChange':bp['renderCommits']-ap['renderCommits'],'averageFpsChange':bp['averageFps']-ap['averageFps'],
  'minRolling60FpsChange':bp['minRolling60HardwareFrameFps']-ap['minRolling60HardwareFrameFps'],
  'worstGapHardwareFrameChange':bp['maxDrawGapHardwareFrames']-ap['maxDrawGapHardwareFrames'],
  'afterCommitTiming':y['commitTiming']})
assert all(r['renderChange']>=0 and r['minRolling60FpsChange']>=0 and r['worstGapHardwareFrameChange']<=0 for r in rows),'death-case regression'
before=sum(x['performance']['renderCommits'] for x in a['cases']);after=sum(x['performance']['renderCommits'] for x in b['cases'])
report={'result':'PASS','scope':'combined Stage56 versus frozen Stage55' if args.scope=='combined' else 'isolated death-admission native A/B',
 'attribution':'Includes decoder placement, HUD glyph lookup, simulation, and death-admission changes; do not attribute the combined gain to death admission alone.' if args.scope=='combined' else 'Only the death-admission helper and draw-tail admission block differ.',
 'baselineSha256':a['romSha256'],'candidateSha256':b['romSha256'],
 'policy':[{'pending2KiBSlots':[1,2],'lastAdmissionLine':190},{'pending2KiBSlots':[3,4],'lastAdmissionLine':180},{'pending2KiBSlots':[5,12],'lastAdmissionLine':164}],
 'summary':{'hardwareFrames':1200,'beforeRenderCommits':before,'afterRenderCommits':after,'renderPercentChange':100*(after/before-1),
  'beforeAggregateAverageFps':before*a['cases'][0]['performance']['hardwareHz']/1200,
  'afterAggregateAverageFps':after*b['cases'][0]['performance']['hardwareHz']/1200,
  'minimumAfterRolling60Fps':min(x['performance']['minRolling60HardwareFrameFps'] for x in b['cases']),
  'rollingWindowHardwareFrames':60,'rollingWindowSeconds':60/b['cases'][0]['performance']['hardwareHz'],
  'minimumAfterRollingWindowRenderCommits':round(min(x['performance']['minRolling60HardwareFrameFps'] for x in b['cases'])*60/b['cases'][0]['performance']['hardwareHz']),
  'latestRecordedSmallCommitEndLine':max(max(x['commitTiming']['ends']) for x in b['cases']),
  'allFixturesEqual':True,'allGameplayOutcomesEqual':True,'formatMismatchFrames':0,'vblankOverruns':0},
 'cases':rows,'stress':b['stress'],
 'limits':['Eight seeded150-hardware-frame kill/death/recovery cases; not exclusively active-animation FPS.',
  'Native mGBA timing, not physical hardware or unrestricted campaign coverage.',
  'Observed VCOUNT bounds are evidence for these cases; mixed-audio phase stress is a separate gate.',
  'No minimum24FPS claim: some rolling60-frame windows remain below24FPS.',
  'Pixel equality and read-only admission logic are covered by the separate production-function ASan/UBSan host suite.']}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'result':report['result'],**report['summary']},indent=2))
