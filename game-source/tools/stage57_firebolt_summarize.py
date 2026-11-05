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
md='''# Stage57 Firebolt 원인 분석

생산 Stage56 소스·ROM은 수정하지 않았습니다. 몬스터가 없는 동일 L1 장면에서 실제 B 입력으로 비교했습니다. 새로 전체 컴파일한 기준 ROM은 배포된 Stage56과 바이트 단위로 같습니다.

| 조건 | 평균 FPS | 최저 약1초 FPS | 최대 갱신 간격 |
|---|---:|---:|---:|
| 정지 |59.73|59.73|16.7ms|
| Firebolt 1회, 3초 관찰 |44.13|38.82|50.2ms|
| Firebolt 6회, 8초 관찰 |41.44|37.83|50.2ms|
| 1회, 효과음만 끔 |44.13|38.82|50.2ms|
| 1회, 투사체 광원만 제외한 진단 ROM |56.74|52.76|50.2ms|
| 6회, 투사체 광원만 제외한 진단 ROM |57.99|52.76|50.2ms|
| 1회, 화면 밖 투사체 광원만 제외한 진단 ROM |53.42|43.80|50.2ms|
| 6회, 화면 밖 투사체 광원만 제외한 진단 ROM |45.79|40.81|50.2ms|

큰 지속 비용은 움직이는 불빛의 160행 하드웨어 윈도 테이블 재생성입니다. 화면 밖 투사체까지 먼저 캐시 키를 변경한 뒤 빌더 안에서 제외하므로, 화면에서 사라진 이후에도 같은 영웅 조명 테이블을 반복 생성합니다. 화면 밖 구간을 같은 HW100–180으로 비교하면 39.82→58.99FPS입니다. 두 진단 ROM은 영웅 조명·마법 그림·마법 연산·음성을 유지했고 시전 수, 마나, 시뮬레이션 틱 수가 기준과 일치합니다.

시각적으로는 투사체 위치 보간이 없습니다. 약19.91Hz 게임 틱에서 이동한 좌표를 그대로 표시하여 이 장면에서는 가로8·세로4픽셀씩 뛰며, 실제 OAM 반영 간격도1–4 하드웨어 프레임으로 불규칙합니다. 렌더 FPS만 올려도 이 계단식 이동은 남습니다.

시전 준비 자세도 오래 유지됩니다. 전사 준비 동작은 첫13틱을 그림0–2에 배분하여 첫 그림이352ms, 다음 그림이335ms 유지됩니다. 발사 전 렌더링은56.74FPS이므로 이 정지는 화면 전체 FPS와 다른 문제입니다.

효과음 제외 시 렌더 횟수·간격이 정확히 동일했습니다. 지형·깊이 캐시의 시작/종료 해시와 관찰한 dirty 슬롯도 동일했고 하드웨어 조명이 계속 켜져 있었습니다. 이 정지 장면에서 지형 전체 재합성이 주원인이라는 증거는 없습니다.

우선 개선 방향은 화면 밖/기존 조명 안에 완전히 포함된 투사체가 조명 캐시를 깨지 않게 하기, 움직이는 광원 테이블 생성 범위 축소, 게임 판정은 유지하며 투사체 표시 좌표만 보간하기입니다. 준비 동작은 원래 발사 시점과 별도로 프레임 배분을 검토할 수 있습니다. 투사체 조명을 끈 진단에서도 첫 발사50.2ms 지연1회가 남아, 최초 자세 처리의 잔여 비용은 별도 계측이 필요합니다.

제한: 실기 측정이 아닙니다. 전사·Firebolt 1레벨·L1 단독 장면입니다. 3초 관찰은 화면 밖 비행을 포함하며 발사체가 살아 있는 상태에서 끝납니다. 벽 충돌의3프레임 구간 FPS는 지속 성능으로 해석하지 않습니다. 진단 ROM은 출하 변경이 아니며 조명 회귀 검증도 완료하지 않았습니다.
'''
(a.folder/'analysis.md').write_text(md)
print(json.dumps({'result':'PASS','summary':str(a.folder/'summary.json'),'metrics':len(metrics),'controls':len(controls)},ensure_ascii=False))
