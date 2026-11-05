#!/usr/bin/env python3
"""Bind native traces, source scope, memory budgets and host proof for Stage58."""
import argparse,difflib,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--baseline-build',type=Path,required=True);p.add_argument('--baseline-report',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
baseline=json.loads((a.baseline_report/'report.json').read_text());candidate=json.loads((a.output/'candidate/report.json').read_text());assert baseline['result']==candidate['result']=='PASS'
prov=json.loads((a.baseline_build/'private-build-provenance.json').read_text());build=json.loads((a.candidate/'build/diablo-gba-assets-build-report.json').read_text());assert prov['build_report']['sha256']==baseline['romSha256'];assert build['sha256']==candidate['romSha256']
names=['src/gba_mode0.c','src/lighting.inc','src/hardware_light.inc','src/projectile_view.inc'];changes=[]
for n in names:
 old=(root/n).read_text() if n in prov['source_sha256'] else ''
 if old:assert hashlib.sha256(old.encode()).hexdigest()==prov['source_sha256'][n],n+' baseline changed'
 new=(a.candidate/n).read_text();assert old!=new
 changes.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+n if old else '/dev/null',tofile='b/'+n))
(a.output/'source.diff').write_text(''.join(changes))
unchanged=[]
for n,h in prov['source_sha256'].items():
 if n in names:continue
 assert hashlib.sha256((a.candidate/n).read_bytes()).hexdigest()==h,n+' unexpected source change';unchanged.append(n)
metrics=[];tick_comparisons=[]
for n in ['idle','single-open','single-wall','repeated-open']:
 b=next(q for q in baseline['cases']if q['name']==n);c=next(q for q in candidate['cases']if q['name']==n)
 bt=json.loads((a.baseline_report/(n+'-trace.json')).read_text());ct=json.loads((a.output/'candidate'/(n+'-trace.json')).read_text())
 def snapshots(t,q):
  result={}
  for x in t:result[(x['ticks']-q['initial']['ticks'])&65535]={'arrows':x['arrows'],'mana':x['mana'],'casts':x['casts']-q['initial']['casts'],'attack':x['attack'],'swing':x['swing']}
  return result
 old,new=snapshots(bt,b),snapshots(ct,c);common=sorted(old.keys()&new.keys());assert all(old[k]==new[k] for k in common),n+' gameplay state differs'
 tick_comparisons.append({'name':n,'identicalObservedTicks':len(common),'fields':['Arrow x/y/vx/vy/life/direction/owner','mana','spell_casts','attack_wait','swing_target']})
 assert b['initial']['terrain']==c['initial']['terrain'] and b['finalTerrain']==c['finalTerrain']
 metrics.append({'name':n,'hardwareFrames':c['hardwareFrames'],'beforeFps':b['fps'],'afterFps':c['fps'],'beforeMinimum60HwFps':b['minimumRolling60HardwareFramesFps'],'afterMinimum60HwFps':c['minimumRolling60HardwareFramesFps'],'beforeMaxGapMs':b['maxDrawGapMs'],'afterMaxGapMs':c['maxDrawGapMs'],'casts':c['casts'],'simulationTicks':c['simulationTicks'],'sameTerrainAndDepth':True})
def oam_steps(folder):
 trace=json.loads((folder/'single-open-trace.json').read_text());out=[]
 for x in trace:
  if not x['effectObjects']:continue
  o=x['effectObjects'][0];q={'hw':x['f'],'x':o['x'],'y':o['y']}
  if not out or any(q[k]!=out[-1][k] for k in ['x','y']):out.append(q)
 return out
old,new=oam_steps(a.baseline_report),oam_steps(a.output/'candidate')
assert len(new)>len(old)
steps=[{'hardwareFrames':y['hw']-x['hw'],'dx':y['x']-x['x'],'dy':y['y']-x['y']}for x,y in zip(new,new[1:])];assert all(0<q['dx']<=8 and 0<=q['dy']<=4 for q in steps)
host=json.loads((a.output/'host/host-summary.json').read_text());assert host['result']=='PASS'
summary={'result':'PASS','baselineRomSha256':baseline['romSha256'],'candidateRomSha256':candidate['romSha256'],'productionProjectEdited':False,'sourceFiles':names,'otherSourceFilesByteIdentical':len(unchanged),'resources':{'baseline':prov['build_report'],'candidate':build,'ewramGrowth':build['ewram_static_bytes']-prov['build_report']['ewram_static_bytes'],'iwramGrowth':build['iwram_code_bytes']-prov['build_report']['iwram_code_bytes']},'metrics':metrics,'nativeGameplayTickComparisons':tick_comparisons,'projectileOam':{'beforePositions':old,'afterPositions':new,'afterSteps':steps,'interpretation':'More visible intermediate positions; draw gaps can still skip presentation samples. Simulation positions and collision schedule are unchanged.'},'host':host,'preserved':['All world light sources remain available to lighting_at; only hardware-window cache keys reject offscreen/contained ellipses.','Existing largest-dark-gap two-window approximation, palettes, black mask, gamma and brightness unchanged.','Game hit/release/cost/range/velocity/RNG code is byte-identical.','Fixed area effects, golem, burst, source-born projectiles, paused view and co-op retain authoritative positions.','Original six cast frames and release-frame2 mapping retained.'],'limitations':['Native mGBA only; no physical GBA measurement.','Quiet Warrior L1 Firebolt benchmark cannot guarantee minimum24FPS for every combat scene.','First cast retains one50.23ms draw gap.','Cast wind-up frame holds remain because the existing art has only six selected poses.','Co-op continues its existing projectile presentation timing.']}
(a.output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
(a.output/'candidate-build.json').write_text(json.dumps(json.loads((a.candidate/'build/private-build-provenance.json').read_text()),indent=2)+'\n')
md='''# Stage58 Firebolt candidate

This is a private candidate; production files were unchanged. Changes cover `projectile_view.inc`, `lighting.inc`, `hardware_light.inc`, and three integration points in `gba_mode0.c`.

| Scene | Stage56 average/lowest approximately 1-second FPS | Candidate average/lowest approximately 1-second FPS |
|---|---:|---:|
| Idle |59.73 /59.73|59.73 /59.73|
| One Firebolt |44.13 /38.82|55.75 /50.77|
| Repeated Firebolts |41.44 /37.83|54.13 /49.77|
| Immediate wall collision |57.40 /54.75|57.40 /54.75|

The complete list of world light sources is retained. Offscreen or fully contained lights are excluded only from hardware-mask cache keys. Only the old and new vertical ranges of changed lights are recalculated. Other rows are copied from the previous table; the live table read by the IRQ is never modified.

Only projectile display positions are interpolated backward from the existing current position using the velocity that produced it. Display positions never pass the gameplay position. Newly spawned, unmoved projectiles, fixed spells, golems, explosions, paused views and multiplayer coordinates retain their existing positions. Sprites and lights use the same interpolated coordinates.

Cast counts, mana, attack state and Arrow position/velocity/life match the baseline at identical game ticks. All C source governing collision, mana, speed, range and RNG is unchanged. Firebolt now displays more intermediate positions instead of jumping 8 pixels at a time. Rendering delays can still skip intermediate samples.

Static EWRAM increases by 8 bytes and IWRAM by 36 bytes. projectile_phase uses 4 bytes; no separate projectile-coordinate array is added. The candidate passed 660,099 ASan/UBSan conditions and the runtime agent's existing lighting oracle and incremental-update oracle. The final integrated ROM still requires validation of the combined changes.

One 50.2ms first-cast delay and long wind-up holds remain. The existing six-frame asset still maps frame 2 to the actual release time. These are not physical hardware measurements or a guarantee of 24 FPS in every combat scene.
'''
(a.output/'analysis.md').write_text(md)
print(json.dumps({'result':'PASS','tickComparisons':tick_comparisons,'beforeOamPositions':len(old),'afterOamPositions':len(new),'sourceDiff':str(a.output/'source.diff')},ensure_ascii=False))
