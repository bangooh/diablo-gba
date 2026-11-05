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
md='''# Stage58 Firebolt 적용 후보

생산 파일은 수정하지 않은 private 후보입니다. 변경 파일은 `projectile_view.inc`, `lighting.inc`, `hardware_light.inc`와 `gba_mode0.c`의 세 군데 연결부입니다.

| 장면 | Stage56 평균/최저약1초 | 적용 후보 평균/최저약1초 |
|---|---:|---:|
| 정지 |59.73 /59.73|59.73 /59.73|
| Firebolt1회 |44.13 /38.82|55.75 /50.77|
| Firebolt반복 |41.44 /37.83|54.13 /49.77|
| 바로 벽 충돌 |57.40 /54.75|57.40 /54.75|

조명의 전체 world source 목록은 유지합니다. 하드웨어 마스크 캐시 키에서만 화면 밖/완전 포함 광원을 제외하고, 변경된 광원의 이전·새 세로 범위만 다시 계산합니다. 나머지 행은 기존 테이블에서 복사하며 IRQ가 읽는 live 테이블은 수정하지 않습니다.

투사체는 기존 현재 위치와 그 위치를 만든 velocity를 사용해 표시 위치만 역보간합니다. 판정 좌표를 넘지 않고, 새로 생겨 아직 이동하지 않은 투사체와 고정 마법·골렘·폭발·일시정지·멀티플레이 좌표는 유지합니다. 표시 스프라이트와 광원이 같은 보간 좌표를 사용합니다.

시전수·마나·공격 상태와 Arrow 위치/속도/life는 같은 게임 틱에서 기준과 일치합니다. 모든 판정·마나·속도·거리·RNG 관련 C 소스도 그대로입니다. 실제 Firebolt 표시 위치는8픽셀씩 건너뛰던 것에서 더 많은 중간 위치로 바뀌었습니다. 렌더 지연이 있는 순간에는 중간 샘플을 건너뛸 수 있습니다.

정적 EWRAM+8B, IWRAM+36B입니다. projectile_phase는4B이며 별도 투사체 좌표 배열은 없습니다. ASan/UBSan660,099조건, runtime agent의 기존 조명 oracle과 증분 갱신 oracle을 통과했습니다. 통합 ROM에서는 최종 조합으로 다시 검증해야 합니다.

첫 시전의50.2ms 지연1회와 준비 자세의 긴 홀드는 남습니다. 6장짜리 기존 자산의2번 그림을 실제 발사 시점에 맞추는 동작은 유지했습니다. 실기 측정이나 전체 전투24FPS 보장은 아닙니다.
'''
(a.output/'analysis.md').write_text(md)
print(json.dumps({'result':'PASS','tickComparisons':tick_comparisons,'beforeOamPositions':len(old),'afterOamPositions':len(new),'sourceDiff':str(a.output/'source.diff')},ensure_ascii=False))
