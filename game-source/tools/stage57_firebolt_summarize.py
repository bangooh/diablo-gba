#!/usr/bin/env python3
"""Summarize native Firebolt controls without treating short phases as steady FPS."""
import argparse,collections,difflib,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--private',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];hz=16777216/280896
reports={n:json.loads((a.folder/n/'report.json').read_text()) for n in ['production','audio-control','no-arrow-light','visible-arrow-light']}
assert all(r['result']=='PASS' for r in reports.values())
assert reports['production']['romSha256']==reports['audio-control']['romSha256']=='d883e470dd4a4ab0e448f872025a3205b419795e63de4b13ab85afd7cc462b04'
def case(v,n):return next(c for c in reports[v]['cases'] if c['name']==n)
def trace(v,n):return json.loads((a.folder/v/(n+'-trace.json')).read_text())
def window(v,n,first,last):
    t=trace(v,n);c=case(v,n);before=t[first-2]['renders'] if first>1 else c['initial']['renders'];renders=t[last-1]['renders']-before
    return {'firstHardwareFrame':first,'lastHardwareFrame':last,'renders':renders,'hardwareFrames':last-first+1,'fps':renders*hz/(last-first+1)}
metrics=[]
for v in reports:
    for c in reports[v]['cases']:
        t=trace(v,c['name']);metrics.append({'variant':v,'name':c['name'],'hardwareFrames':c['hardwareFrames'],'fps':c['fps'],'minimumRolling60HardwareFramesFps':c['minimumRolling60HardwareFramesFps'],'maxDrawGapMs':c['maxDrawGapMs'],'renders':c['renders'],'casts':c['casts'],'simulationTicks':c['simulationTicks'],'finalMana':t[-1]['mana'],'terrainEndpointHashesEqual':c['terrainCacheUnchanged'],'hardwareLightAlwaysEnabled':c['lightHardwareNeverDisabled'],'maximumObservedDirtyTiles':max(f['dirtyTiles'] for f in t)})
tr=trace('production','single-open');c=case('production','single-open')
dwell=[];start=None
for i,f in enumerate(tr):
    if f['attack'] and (start is None or f['heroFrame']!=tr[start]['heroFrame']):
        if start is not None:dwell.append({'frame':tr[start]['heroFrame'],'firstHw':start+1,'lastHw':i,'ms':(i-start)*1000/hz})
        start=i
    elif not f['attack'] and start is not None:
        dwell.append({'frame':tr[start]['heroFrame'],'firstHw':start+1,'lastHw':i,'ms':(i-start)*1000/hz});start=None
oam=[];prev=None
for f in tr:
    key=[(q['x'],q['y']) for q in f['effectObjects']]
    if key and key!=prev:
        oam.append({'hw':f['f'],'positions':key,'gapFromPreviousVisibleChange':f['f']-oam[-1]['hw'] if oam else None})
    prev=key
controls=[]
for v in ['no-arrow-light','visible-arrow-light']:
    for n in ['single-open','repeated-open']:
        base=case('production',n);q=case(v,n);bt=trace('production',n);qt=trace(v,n)
        assert (base['casts'],base['simulationTicks'],bt[-1]['mana'])==(q['casts'],q['simulationTicks'],qt[-1]['mana'])
        assert base['initial']['terrain']==q['initial']['terrain'] and base['finalTerrain']==q['finalTerrain']
        controls.append({'variant':v,'name':n,'sameCastsManaAndTicks':True,'sameInitialAndFinalTerrainAndDepth':True,'fpsDelta':q['fps']-base['fps']})
sound=case('audio-control','no-sfx-open');soundbase=case('audio-control','single-open')
assert all(sound[k]==soundbase[k] for k in ['renders','fps','gaps','minimumRolling60HardwareFramesFps'])
provenance=json.loads((a.baseline/'private-build-provenance.json').read_text());assert provenance['build_report']['sha256']==reports['production']['romSha256']
(a.folder/'verified-baseline-build.json').write_text(json.dumps(provenance,indent=2)+'\n')
original=(root/'src/lighting.inc').read_text()
for v in ['no-arrow-light','visible-arrow-light']:
    changed=(a.private/v/'lighting.inc').read_text();(a.folder/(v+'.diff')).write_text(''.join(difflib.unified_diff(original.splitlines(True),changed.splitlines(True),fromfile='production/src/lighting.inc',tofile='diagnostic-only/src/lighting.inc')))
    prov=json.loads((a.private/v/'build/private-build-provenance.json').read_text());assert prov['build_report']['sha256']==reports[v]['romSha256']
    assert prov['build_report']['iwram_code_bytes']==24264 and prov['build_report']['ewram_static_bytes']==261904
    (a.folder/(v+'-build.json')).write_text(json.dumps(prov,indent=2)+'\n')
summary={'result':'PASS','productionRomSha256':reports['production']['romSha256'],'productionSourceAndRomUnchanged':True,'physicalGba':False,'fixture':reports['production']['fixture'],'metrics':metrics,'controls':controls,'sfxControlIdenticalRenderCadence':True,'offscreenFixedWindow':[{'variant':v,**window(v,'single-open',100,180)} for v in ['production','no-arrow-light','visible-arrow-light']],'observedHeroCastFrameDwell':dwell,'observedProjectileOamChanges':oam,'findings':[
 {'cause':'Moving projectile light-window rebuilds','confidence':'Causal private ROM control','source':['src/lighting.inc:36-47','src/hardware_light.inc:73-89','src/hardware_light.inc:19-70'],'detail':'Every changed projectile screen position changes the source cache key and invokes a 160-row window rebuild. Omitting only arrow lights raises repeated-cast average from41.44 to57.99 FPS while preserving spell simulation, sprites, audio and hero light.'},
 {'cause':'Offscreen projectiles still invalidate lighting window cache','confidence':'Causal private ROM control','source':['src/hardware_light.inc:79-88','src/hardware_light.inc:27-28'],'detail':'Offscreen rejection occurs inside the builder after cache invalidation. Filtering only offscreen arrow sources before cache keys restores the fixed offscreen window from39.82 to58.99 FPS.'},
 {'cause':'Projectile display has no render interpolation','confidence':'Source plus native RAM/OAM trace','source':['src/gba_mode0.c:1542-1543','src/spells.c:409-425','src/gba_mode0.c:1261-1264'],'detail':'Simulation is about19.91 Hz; Arrow positions are passed directly to OAM. In the fixture it jumps8 horizontal and4 vertical pixels per simulation step. Actual visible OAM updates have irregular1–4 HW intervals; it is not a uniform20FPS display.'},
 {'cause':'Long holds in cast animation wind-up','confidence':'Source plus native hero-frame trace','source':['src/spells.c:97-98','src/gba_mode0.c:1282-1285'],'detail':'Warrior wind-up maps its first13 ticks to animation frames0–2. Observed frame0 holds352ms and frame1 holds335ms even though pre-release rendering averages56.74 FPS.'}],
 'excludedOrUnproven':['Disabling only sound effects preserves exactly the render cadence in the tested cast; BGM remains active.','Terrain cache endpoints and sampled dirty slots are unchanged and hardware lighting remains enabled. Source shows no geometry/light-band invalidation in this stationary fixture, but internal terrain-call cycle counters were not added.','First release still has one50.23ms draw gap with projectile lights omitted; the exact residual cost has not been isolated.','Private no-arrow-light ROM is a diagnostic control, not a proposed shipped change. Offscreen-only variant is also unshipped and has not passed a full lighting regression suite.'],
 'limits':['Quiet single-player L1 Warrior with learned level1 Firebolt; it does not estimate all spells/classes/floors or crowded combat.','Single-open180HW includes offscreen flight and ends while projectile remains active; active-window FPS is not a complete projectile-lifetime average.','Near-wall projectile exists only3HW; its phase FPS is too short for a sustainable-rate claim.','Hardware-frame-end PC samples are coarse attribution, not cycle percentages.','Fresh baseline compiled from all sources reproduces the exact published ROM bytes; variants recompile onlygba_mode0.c with the isolated lighting include.']}
(a.folder/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
md='''# Stage57 Firebolt cause analysis

The production Stage56 source and ROM were unchanged. Tests used actual B input in the same monster-free L1 scene. A full rebuild of the baseline matched the released Stage56 ROM byte for byte.

| Condition | Average FPS | Lowest approximately 1-second FPS | Maximum update gap |
|---|---:|---:|---:|
| Idle |59.73|59.73|16.7ms|
| One Firebolt, 3-second observation |44.13|38.82|50.2ms|
| Six Firebolts, 8-second observation |41.44|37.83|50.2ms|
| One cast, sound effects disabled only |44.13|38.82|50.2ms|
| One cast, diagnostic ROM excluding projectile lights only |56.74|52.76|50.2ms|
| Six casts, diagnostic ROM excluding projectile lights only |57.99|52.76|50.2ms|
| One cast, diagnostic ROM excluding offscreen projectile lights only |53.42|43.80|50.2ms|
| Six casts, diagnostic ROM excluding offscreen projectile lights only |45.79|40.81|50.2ms|

The main sustained cost is rebuilding the 160-row hardware window table for moving lights. Offscreen projectiles change the cache key before being rejected inside the builder, repeatedly regenerating the same hero lighting table after they leave the screen. Comparing the same offscreen interval, HW100-180, gives 39.82 to 58.99 FPS. Both diagnostic ROMs retain hero lighting, spell graphics, spell simulation and audio; cast counts, mana and simulation ticks match the baseline.

Projectile positions have no visual interpolation. Coordinates from the approximately 19.91 Hz game tick are displayed directly, jumping 8 horizontal and 4 vertical pixels in this scene. Actual OAM update intervals are irregular, spanning 1-4 hardware frames. Increasing render FPS alone does not remove this stepped motion.

Casting wind-up poses also persist for a long time. The Warrior maps the first 13 ticks to frames 0-2, holding the first image for 352ms and the next for 335ms. Pre-release rendering averages 56.74 FPS, so these holds are distinct from overall rendering performance.

Disabling sound effects leaves render counts and intervals exactly unchanged. Initial/final terrain and depth cache hashes and observed dirty slots also match, with hardware lighting continuously enabled. There is no evidence that full terrain recomposition is the main cause in this stationary scene.

Priorities are to prevent offscreen or fully contained projectile lights from invalidating the lighting cache, reduce the table rows regenerated for moving lights, and interpolate display positions while preserving gameplay coordinates. Wind-up frame allocation can be reviewed independently of the original release timing. Even with projectile lights disabled, the first release retains one 50.2ms delay; its remaining cost requires separate measurement.

Limitations: these are not physical hardware measurements. The fixture is a solo Warrior with level-1 Firebolt on L1. The 3-second observation includes offscreen flight and ends while the projectile is still alive. FPS during the 3-frame wall-collision interval is not a sustained rate. Diagnostic ROMs are not shipping changes and have not completed lighting regression validation.
'''
(a.folder/'analysis.md').write_text(md)
print(json.dumps({'result':'PASS','summary':str(a.folder/'summary.json'),'metrics':len(metrics),'controls':len(controls)},ensure_ascii=False))
