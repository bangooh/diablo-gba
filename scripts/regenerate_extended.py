"""Generate remaining assets and optionally build the MPQ-derived ROM."""
import argparse,ast,hashlib,json,os,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mpq',type=Path,required=True)
p.add_argument('--foundation',type=Path,required=True)
p.add_argument('--zig',type=Path,help='Compile the complete generated set after all stages pass')
a=p.parse_args();base=a.foundation.resolve();game=base/'game-source';mpq=str(a.mpq.resolve())
report=json.loads((base/'regeneration-report.json').read_text())
if any(s['returncode'] for s in report['steps']):p.error('Resolve foundation failures before continuing')
if not any(s['name']=='verify_lightning_pixels' and s['returncode']==0 for s in report['steps']):p.error('Run foundation with --actors first')
for name in ('tools','src'):
 shutil.copytree(ROOT/'game-source'/name,game/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
shutil.copy2(ROOT/'game-source/link.ld',game/'link.ld')
for f in (ROOT/'game-source/project_data').iterdir():
 if f.suffix=='.h':shutil.copy2(f,game/'generated'/f.name)
for name in ('quest_remaining','butcher','finale','objects','monster_stage66','monster_stage71','combat','exocet','ending','frontend_stage81','environment_stage61','environment_stage64','environment_stage76'):
 (game/'source_assets'/name).mkdir(exist_ok=True)
shutil.copytree(ROOT/'game-source/source_assets/exocet',game/'source_assets/exocet',dirs_exist_ok=True)
state=base/'extended-stages.json';done=json.loads(state.read_text()) if state.exists() else []
logs=base/'extended-logs';logs.mkdir(exist_ok=True);position=0;rerun=False
context=hashlib.sha256()
for directory in (game/'src',):
 for source in sorted(directory.rglob('*')):
  if source.is_file() and source.suffix in ('.py','.c','.h','.inc','.json') and '__pycache__' not in source.parts:
   context.update(str(source.relative_to(directory)).encode());context.update(source.read_bytes())
for source in [ROOT/'docs/native-input-map.json',ROOT/'docs/audio-master-inputs.json',ROOT/'docs/native-palette-checks.json',*sorted((ROOT/'game-source/source_assets/exocet').glob('*.bin')),*sorted((ROOT/'game-source/project_data').glob('*.h'))]:
 context.update(source.name.encode());context.update(source.read_bytes())
context_digest=context.hexdigest()

def dependency_digest(script):
 pending=[script.resolve()];seen=set();h=hashlib.sha256()
 while pending:
  source=pending.pop()
  if source in seen:continue
  seen.add(source);data=source.read_bytes();h.update(source.name.encode());h.update(data)
  for node in ast.walk(ast.parse(data)):
   names=[n.name for n in node.names] if isinstance(node,ast.Import) else [node.module] if isinstance(node,ast.ImportFrom) and node.module else []
   for name in names:
    for parent in (source.parent,game/'tools'):
     candidate=parent/(name.replace('.', '/')+'.py')
     if candidate.is_file():pending.append(candidate.resolve());break
 return h.hexdigest()

def run(name,script,*args):
 global position,rerun
 args=list(map(str,args));digest=hashlib.sha256(script.read_bytes()).hexdigest()
 record=dict(name=name,script_sha256=digest,dependencies_sha256=dependency_digest(script),context_sha256=context_digest,args=args)
 if position<len(done) and all(done[position].get(k)==v for k,v in record.items()) and not rerun:
  print(name,'REUSED',flush=True);position+=1;return
 if position<len(done):
  raise RuntimeError('Completed stage changed: '+name+'. Use a new output directory; later stages may have overwritten its inputs.')
 rerun=True;del done[position:]
 protected={'pack_monsters_stage66':('floor_decals.rle','floor_decals.h','monster_death.h'),
            'pack_monsters_stage71':('floor_decals.rle','floor_decals.h')}.get(name)
 if protected:
  checkpoint=base/'input-checkpoints'/name
  marker=checkpoint/'manifest.json'
  if not marker.exists():
   checkpoint.mkdir(parents=True,exist_ok=True);hashes={}
   for filename in protected:
    data=(game/'generated'/filename).read_bytes();(checkpoint/filename).write_bytes(data);hashes[filename]=hashlib.sha256(data).hexdigest()
   marker.write_text(json.dumps(hashes,indent=2)+'\n')
  hashes=json.loads(marker.read_text())
  for filename in protected:
   data=(checkpoint/filename).read_bytes()
   if hashlib.sha256(data).hexdigest()!=hashes[filename]:raise ValueError('Corrupt input checkpoint: '+filename)
   (game/'generated'/filename).write_bytes(data)
 start=time.monotonic()
 with (logs/(name+'.log')).open('w') as log:
  result=subprocess.run([sys.executable,str(script),*args],stdout=log,stderr=subprocess.STDOUT)
 if result.returncode:
  state.write_text(json.dumps(done,indent=2)+'\n')
  print(name,'FAILED; see',logs/(name+'.log'),flush=True);sys.exit(result.returncode)
 record['seconds']=round(time.monotonic()-start,2);done.append(record);position+=1
 state.write_text(json.dumps(done,indent=2)+'\n');print(name,'PASS',flush=True)

def tool(name,*args):run(name,game/'tools'/(name+'.py'),*args)
run('native-inputs',ROOT/'scripts/restore_native_inputs.py','--mpq',mpq,'--project',game)
for name in ('pack_more_monsters','pack_floor_actors','pack_props'):tool(name,mpq)
tool('compress_assets',game/'generated')
tool('pack_butcher',mpq)
tool('pack_dungeon',game/'source_assets/cathedral',game/'generated')
for name in ('pack_cathedral_details','pack_quest_rock','pack_quest_valor','pack_quest_remaining','pack_blind_room','pack_valor_room'):tool(name,mpq)
tool('pack_banner_room');tool('pack_world_terrain',mpq);tool('pack_quest_banner');tool('pack_king_quest')
for name in ('pack_water_candles','pack_finale','pack_late_quests','pack_regional_doors','pack_explosive_barrels','pack_wall_traps'):tool(name,mpq)
tool('pack_ground_items',mpq,'--legacy-palette');tool('pack_floor_decals',mpq)
tool('pack_frontend',mpq);tool('pack_front_screens',mpq)
tool('pack_exocet_font');tool('pack_town_dialogue_font');tool('pack_frontend_stage81','--mpq',mpq)
tool('make_prop_highlight');tool('pack_water_transition')
tool('audit_background_palettes','--output',game/'generated/background-palette-review')
run('snapshot-background',ROOT/'scripts/snapshot_background_inputs.py','--project',game)
tool('pack_bg_water_palette');tool('pack_door_arches');tool('pack_environment_stage61')
tool('pack_environment_stage64','--mpq',mpq);tool('pack_quest_objects_stage71');tool('pack_environment_stage76',mpq)
for name in ('pack_fixed_rooms','pack_fixed_l1_stage76','pack_anvil_guards'):tool(name)
tool('pack_ai_specials',mpq)
for name in ('pack_shop_costs','pack_theme_rules_stage64','pack_town_minimap_stage80'):tool(name)
for name in ('pack_combat','pack_monster_audio','pack_item_audio'):tool(name,mpq)
tool('pack_quest_audio');tool('pack_levelup',mpq)
for name in ('stage53_boss_catalog','pack_boss_intro_audio','stage53_hero_error_catalog','pack_hero_error_audio'):tool(name)
tool('pack_hazard_audio',mpq,'--intermediate-master')
tool('pack_spells',mpq);tool('pack_spell_expansion',mpq)
tool('prepare_mpq_presentation','--mpq',mpq)
tool('pack_ending_audio');tool('pack_menu_audio')
run('bow-master',game/'tools/pack_bow.py',mpq)
run('audio-masters',ROOT/'scripts/prepare_audio_masters.py','--project',game)
run('ground-items-final',game/'tools/pack_ground_items.py',mpq)
tool('pack_monsters_stage66','--mpq',mpq,'--baseline',game)
tool('pack_monsters_stage71','--mpq',mpq,'--baseline',game)
tool('import_monster_audio_stage71',mpq)
tool('pack_audio_stage66','--mpq',mpq)
tool('pack_music_regions_stage71');tool('pack_music_delta')
run('gold-audio-final',game/'tools/pack_audio134.py',mpq)
run('cow-audio-final',game/'tools/pack_entry_cows.py',mpq)
tool('pack_ending_video','--smk',game/'source_assets/ending/DiabVic1.smk')
tool('pack_ending_common')
monster_inputs=base/'input-checkpoints/monster-sharing'
run('snapshot-monsters',ROOT/'scripts/snapshot_monster_inputs.py','--project',game,'--output',monster_inputs)
tool('share_monster_palettes','--source',monster_inputs,'--palettes',monster_inputs,'--pool',monster_inputs/'hero_frame_pool.lz','--output',game/'generated')
tool('pack_unique_monsters',mpq)
tool('pack_skeleton_spawn',mpq);tool('pack_player_blocks',mpq)
tool('pack_npc_cursor_stage62','--extract',mpq);tool('pack_item_specials');tool('pack_death_red')
tool('restore_native_palettes','--mpq',mpq)
tool('finalize_actor_storage')
run('spell-native-inputs',game/'tools/extract_spell_sources.py',mpq)
tool('pack_spell_geometry_stage74')
tool('pack_spellbook_palette')
tool('restore_l2_arches',mpq);tool('pack_fixed_wall_art');tool('pack_prop_atlas')
tool('make_prop_dirty');tool('pack_floor_masks');tool('pack_light_spans');tool('pack_outer_mask')
tool('pack_town_states','--mpq',mpq)
opening=game/'generated/front_opening_video.lz'
tool('repack_opening',opening,opening,'--report',game/'generated/opening-optimal-report.json','--delta-candidates')
tool('pack_asset_sizes')
import re
header=game/'generated/front_opening.h'
header.write_text(re.sub(r'(FRONT_OPENING_STREAM_BYTES\s*=\s*)\d+',lambda m:m[1]+str(opening.stat().st_size),header.read_text()))
shutil.copy2(ROOT/'docs/native-palette-checks.json',game/'generated/native-palette-checks.json')
(base/'extended-report.json').write_text(json.dumps(dict(full_mpq_rom_verified=False,stages=done),indent=2)+'\n')
if a.zig:
 subprocess.run([sys.executable,str(ROOT/'game-source/build.py'),'--zig',str(a.zig.resolve()),'--source-root',str(game),'--output',str(base/'rom')],check=True)
print('Generation stages completed; inspect build and runtime validation separately.',flush=True)
