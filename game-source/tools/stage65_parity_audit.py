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
lines=['# Monsters, ordinary uniques and regional BGM vs the original — Stage65', '',
 'This compares the preserved retail Diablo source with the current runtime source. Hellfire, originally disabled base monsters and unused table entries are excluded from missing normally spawning monster counts. This stage changes the shop UI; it does not add monsters or regional music.', '',
 '## Base monsters', '',
 f'There are {len(bodies)+len(extras)} runtime kinds ({len(bodies)} base/quest enemies, 4 ordinary unique leaders and 4 separate minion IDs). Of the 88 active original base-monster entries, {len(missing)} have no corresponding body definition. Comparisons include original AI and graphics paths as well as names. A matching body does not establish full AI or ordinary spawn parity.', '',
 '| Original AI family | Missing kinds/variants |','| --- | --- |']
for ai,names in sorted(groups.items()):lines.append('| '+ai+' | '+', '.join(names)+' |')
lines+=['','All four variants are missing in each of the BAT, ACID, MAGMA, STORM, GARG, MEGA and SNAKE families. Other omissions are individual variants within existing families. Skeleton, Stone Clan and Night Clan refer to archer variants here; a melee variant with the same name does not count as implementing the archer.', '',
 '## Ordinary unique monsters', '',
 'The four connected ordinary uniques are **Bonehead Keenaxe, Rotcarnage, Breakspine and Blackstorm**, on floors 2, 3, 9 and 10 respectively. Quest bosses such as the Butcher, Leoric and Lazarus are excluded from these four.', '',
 'The original ordinary-unique table has 87 rows. Filtering only for floors 1-16 and active base monsters leaves 72 candidates; 68 remain unconnected after subtracting the four implemented rows. Actual spawning also requires the floor monster-roster conditions in the original `PlaceUniques`, so this does not mean 68 monsters are missing from every campaign. The remaining 15 disabled/out-of-range rows are excluded from normal-spawn omissions.', '',
 'Unconnected examples: Bladeskin the Slasher, Pukerat the Unclean, Boneripper, Rotfeast the Hungry, Bongo, Shadowbite, Deadeye, Blackash the Burning, Bloodskin Darkbow, Deathshade Fleshmaul, The Flayer, Witchfire the Unholy and Sir Gorash. Full names, floors, base types and connection status are in `generated/stage65-validation/parity/ordinary-uniques.csv`.', '',
 '## Regional BGM', '',
 '| Region | Original | Current |','| --- | --- | --- |','| Town | DTowne.wav | Implemented |','| Cathedral, floors 1-4 | DLvlA.wav | Implemented |','| Catacombs, floors 5-8 | DLvlB.wav | Missing; reuses Cathedral music |','| Caves, floors 9-12 | DLvlC.wav | Missing; reuses Cathedral music |','| Hell, floors 13-16 | DLvlD.wav | Missing; reuses Cathedral music |','| Main menu | Dintro.wav | Implemented |', '',
 'Region loading in `src/gba_mode0.c` still uses `audio_start(game.area?0:1)`, routing dungeons and separate quest areas to the same dungeon track. Ending music already has a separate path and is distinguished from missing exploration BGM.', '',
 '## Other remaining differences', '',
 '| Item | Current difference |','| --- | --- |','| Multiplayer-only shrines | Spooky and Tainted remain excluded. Hidden, Gloomy and Weird are implemented. |','| Simultaneous projectiles | Arrows and spells share 8 slots; the original has 125 missile slots. Supporting a spell name does not imply reproducing its full projectile count. |','| Dropping items in town | Dropping ordinary items onto the town floor is blocked. |','| NPC speech | Primarily 8 greetings; full quest speech is absent, following the previously requested space-saving policy. |', '',
 'Evidence: shrine_type_supported in src/environment_objects.inc (18 and 25 excluded), MAX_ARROWS in src/game.h, MAXMISSILES in the original defs.h, the INV_TOWN return in src/items.c, and generated/greeting_audio.h. Approved presentation choices such as downscaled graphics, unified endings and reduced speech are not classified as new bugs.', '',
 '## Changes since the earlier audit', '',
 'Stage64 added 17 theme-room kinds, 4 fountains, Goat Shrine and Cauldron, and permanent Hidden/Gloomy/Weird equipment effects. Their unimplemented labels in Stage61 are historical records and must not be used as the current missing-feature list.', '',
 'Validation compares source and generated tables. It does not guarantee a complete campaign playthrough, exact original AI parity or a minimum of 24 FPS on physical hardware. Original/current file hashes and detailed rows are preserved in `generated/stage65-validation/parity/report.json`.', '']
(ROOT/'PARITY-AUDIT-STAGE65.md').write_text('\n'.join(lines))
print(json.dumps({'result':'PASS','missing_base_rows':len(missing),'ordinary_uniques_present':sorted(leaders),'candidate_general_rows':sum(r['campaign_level_candidate'] for r in unique),'regional_BGM_missing':3}))
