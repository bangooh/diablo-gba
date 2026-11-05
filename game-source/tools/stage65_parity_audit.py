"""Current port vs locally preserved retail Diablo; no Hellfire/dormant inflation."""
from pathlib import Path
import collections,csv,hashlib,json,re,subprocess
from stage61_parity_audit import ROOT,preprocess,monster_rows

out=ROOT/'generated/stage65-validation/parity';out.mkdir(parents=True,exist_ok=True)
native=ROOT/'source_assets/monster_catalog/monstdat.cpp'
raw=monster_rows(native.read_text());source=preprocess(native);base=monster_rows(source)
availability=[int(x) for x in re.search(r'char MonstAvailTbl\[\] = \{(.*?)\};',source,re.S)[1].split(',') if x.strip()]
assert len(base)==len(availability)==112
bodies=json.loads((ROOT/'source_assets/monster_catalog/monsters.json').read_text())['monsters']
extras=json.loads((ROOT/'source_assets/monster_catalog/uniques.json').read_text())['monsters']
present={raw[r['source_id']] for r in bodies}
missing=[{'native_id':i,'name':n,'ai':ai,'graphic':g} for i,(n,ai,g) in enumerate(base) if availability[i] and (n,ai,g) not in present]
groups=collections.defaultdict(list)
for row in missing:groups[row['ai']].append(row['name'])
enums=preprocess(ROOT/'source_assets/item_catalog/enums.h')
ids={k:int(v,0) for k,v in re.findall(r'(MT_\w+)\s*=\s*(0x[\da-fA-F]+|\d+)',enums)}
table=source[source.index('UniqMonstStruct UniqMonst[]'):];table=table[:table.index('\n};')]
rows=re.findall(r'\{\s*(MT_\w+),\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),',table)
assert len(rows)==97 and rows[10][1]=='Bonehead Keenaxe'
leaders={r['name'] for r in extras if r['role']=='leader'}
unique=[]
for typ,name,trn,floor in rows[10:]:
 level=int(floor);eligible=1<=level<=16 and bool(availability[ids[typ]])
 unique.append({'name':name,'base_type':typ,'floor':level,'palette':trn,'base_available':bool(availability[ids[typ]]),'campaign_level_candidate':eligible,'implemented':name in leaders})
assert len(leaders)==4 and all(any(r['name']==n for r in unique) for n in leaders)
sound=(ROOT/'source_assets/town_dialogue_stage53/sound.cpp').read_text()
sound=sound[sound.index('const char *const sgszMusicTracks'):];sound=sound[:sound.index('\n};')+3]
sound=subprocess.check_output(['gcc','-E','-P','-x','c','-'],input=sound,text=True)
tracks=re.findall(r'"([^"]+)"',re.search(r'sgszMusicTracks\[NUM_MUSIC\] = \{(.*?)\};',sound,re.S)[1])
assert len(tracks)==6
audio=(ROOT/'src/audio.c').read_text();scene=(ROOT/'src/gba_mode0.c').read_text()
assert 'audio_start(game.area?0:1);' in scene
music=audio[audio.index('static const Sample music[]'):audio.index('/* Complete native ending tracks')]
assert music.count('AUDIO_MUSIC_')==2 and 'MENU_AUDIO_OUTPUT_SAMPLES' in music
paths=['source_assets/monster_catalog/monstdat.cpp','source_assets/monster_catalog/monsters.json','source_assets/monster_catalog/uniques.json','source_assets/item_catalog/enums.h','source_assets/item_catalog/monster.cpp','source_assets/town_dialogue_stage53/sound.cpp','src/monster_ai.c','src/monster_unique.inc','src/dungeon.c','src/game.h','generated/monster_unique_metadata.inc','src/audio.c','src/gba_mode0.c','src/theme_rooms.inc','src/fountain_objects.inc','src/item_physical.inc','tools/stage65_parity_audit.py','src/environment_objects.inc','src/items.c','source_assets/item_catalog/defs.h','generated/greeting_audio.h']
report={'result':'PASS','scope':'Source audit, not a campaign playthrough. Underlying body identity is not proof of complete AI/spawn parity.',
 'original_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','original_variant':'Retail Diablo; HELLFIRE and SPAWN undefined',
 'monsters':{'runtime_kinds':len(bodies)+len(extras),'main_kinds':len(bodies),'minion_aliases':sum(r['role']=='minion' for r in extras),'available_original_base_rows':sum(bool(v) for v in availability),'missing_base_rows':missing,'missing_by_ai':dict(groups)},
 'ordinary_uniques':{'implemented':sorted(leaders),'original_general_table_rows':len(unique),'campaign_level_base_available_candidates':sum(r['campaign_level_candidate'] for r in unique),'rows':unique,'caution':'Excludes original first10 quest/special entries. Candidate filtering is only floor1..16 and nonzero base availability; PlaceUniques also requires that base type in the actual roster. No overall completion percentage.'},
 'music':{'original_tracks':tracks,'current':['Town','Cathedral (all dungeon regions)','Main menu'],'missing_regional':['Catacombs / DLvlB.wav','Caves / DLvlC.wav','Hell / DLvlD.wav'],'ending_tracks':'Separate existing ending system, not counted as exploration BGM.'},
 'other_remaining':{'multiplayer_shrines':['Spooky','Tainted'],'shared_projectile_slots':8,'original_missile_slots':125,'town_item_drop':'blocked','NPC_voice_policy':'8 greetings; full quest speech omitted by prior user choice'},
 'completed_since_stage61':['17 theme room kinds','4 fountains','Goat Shrine and Cauldron','Hidden/Gloomy/Weird permanent equipment effects'],
 'sourceSha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}}
(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
for name,records in [('missing-monsters.csv',missing),('ordinary-uniques.csv',unique)]:
 with (out/name).open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
lines=['# 원본 대비 몬스터·일반 유니크·지역 BGM — Stage65', '',
 '보존된 Diablo 본편 원본과 현재 실행 소스를 대조했다. Hellfire, 원본에서 비활성인 기본 몬스터, 사용하지 않는 표 항목을 정상 출현 몬스터 누락 수에 합산하지 않았다. 새 몬스터나 지역 음악을 추가한 작업은 아니며, 이번 변경은 상점 UI다.', '',
 '## 기본 몬스터', '',
 f'현재 런타임 종류는 {len(bodies)+len(extras)}개다(본체·퀘스트 적 {len(bodies)}개, 일반 유니크 리더 4개, 해당 부하용 별도 ID 4개). 원본의 활성 기본 몬스터 88개 항목 중 대응하는 본체 정의가 없는 항목은 {len(missing)}개다. 이름뿐 아니라 원본 AI와 그래픽 경로를 함께 비교했다. 본체가 있다고 해서 AI·일반 배치까지 완전히 같다는 뜻은 아니다.', '',
 '| 원본 AI 계열 | 빠진 종류·변종 |','| --- | --- |']
for ai,names in sorted(groups.items()):lines.append('| '+ai+' | '+', '.join(names)+' |')
lines+=['','박쥐(BAT), 산성 짐승(ACID), 마그마(MAGMA), 번개 악마(STORM), 가고일(GARG), 발록(MEGA), 뱀(SNAKE)은 각 4종 계열 전체가 빠져 있다. 나머지는 기존 계열의 일부 변종 누락이다. Skeleton은 궁수형, Stone Clan·Night Clan은 궁수형을 뜻한다. 같은 이름의 근접형이 있다고 해서 해당 변종이 구현된 것으로 세지 않았다.', '',
 '## 일반 유니크 몬스터', '',
 '연결된 일반 유니크는 **Bonehead Keenaxe, Rotcarnage, Breakspine, Blackstorm** 4종이며 출현 층은 각각 2·3·9·10층이다. Butcher, Leoric, Lazarus 등 퀘스트 보스는 이 4종에 포함하지 않았다.', '',
 '원본 일반 유니크 표는 87행이다. 이 중 1~16층과 기본 몬스터 활성 여부만으로 걸러진 후보는 72행이며, 구현된 4행을 뺀 68행에는 현재 연결이 없다. 실제 출현에는 원본 `PlaceUniques`의 해당 층 몬스터 명단 조건도 필요하므로 “매 캠페인에 68마리가 빠진다”는 뜻은 아니다. 나머지 비활성·범위 밖 15행을 정상 출현 누락에 더하지 않았다.', '',
 '미연결 예: Bladeskin the Slasher, Pukerat the Unclean, Boneripper, Rotfeast the Hungry, Bongo, Shadowbite, Deadeye, Blackash the Burning, Bloodskin Darkbow, Deathshade Fleshmaul, The Flayer, Witchfire the Unholy, Sir Gorash. 전체 이름·층·기본 종류·연결 여부는 `generated/stage65-validation/parity/ordinary-uniques.csv`에 있다.', '',
 '## 지역별 BGM', '',
 '| 영역 | 원본 | 현재 |','| --- | --- |','| 마을 | DTowne.wav | 구현 |','| 성당 1~4층 | DLvlA.wav | 구현 |','| 지하묘지 5~8층 | DLvlB.wav | 미구현, 성당 곡 재사용 |','| 동굴 9~12층 | DLvlC.wav | 미구현, 성당 곡 재사용 |','| 지옥 13~16층 | DLvlD.wav | 미구현, 성당 곡 재사용 |','| 메인 메뉴 | Dintro.wav | 구현 |', '',
 '`src/gba_mode0.c`의 지역 로딩은 여전히 `audio_start(game.area?0:1)`이므로 던전·별도 퀘스트 지역은 같은 던전 트랙으로 연결된다. 엔딩 음악은 별도 경로로 이미 있으며 탐험 BGM 누락과 구분한다.', '',
 '## 그 밖에 남아 있는 차이', '',
 '| 항목 | 현재 차이 |','| --- | --- |','| 멀티 전용 쉬라인 | Spooky·Tainted는 여전히 제외한다. Hidden·Gloomy·Weird는 이미 구현했다. |','| 동시 발사체 | 화살·마법 공유8슬롯이며 원본은125 missile 슬롯이다. 같은 마법 이름의 지원과 대량 발사체 재현은 별개다. |','| 마을 아이템 버리기 | 일반 아이템을 마을 바닥에 버리는 동작은 차단되어 있다. |','| NPC 대사 음성 | 인사말8개 위주이며 전체 퀘스트 대사 음성은 없다. 이는 이전에 요청한 용량 절약 정책이다. |', '',
 '근거: src/environment_objects.inc의 shrine_type_supported(18·25 제외), src/game.h의 MAX_ARROWS, 원본 defs.h의 MAXMISSILES, src/items.c의 INV_TOWN 반환, generated/greeting_audio.h. 그래픽 축소·엔딩 통일·음성 축소처럼 승인된 표현 선택을 새 버그로 분류하지 않았다.', '',
 '## 이전 감사 문서와 달라진 부분', '',
 'Stage64에서 테마 방 17종, 분수 4종, 염소 쉬라인·가마솥, Hidden/Gloomy/Weird 장비 영구 효과를 추가했다. 따라서 Stage61 문서에 남아 있는 이 항목들의 “미구현” 표시는 당시 기록이며 현재 누락 목록으로 사용하면 안 된다.', '',
 '검증은 소스와 생성 표의 대조다. 전체 캠페인 플레이·원본 AI 완전 일치·실기 최소 24FPS를 보증하지 않는다. 원본과 현재 파일의 해시 및 상세 행은 `generated/stage65-validation/parity/report.json`에 보존한다.', '']
(ROOT/'PARITY-AUDIT-STAGE65.md').write_text('\n'.join(lines))
print(json.dumps({'result':'PASS','missing_base_rows':len(missing),'ordinary_uniques_present':sorted(leaders),'candidate_general_rows':sum(r['campaign_level_candidate'] for r in unique),'regional_BGM_missing':3}))
