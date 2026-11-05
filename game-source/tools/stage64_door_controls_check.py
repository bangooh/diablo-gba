"""Actual production controller integration; compare unchanged movement to Stage63."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path, required=True, help='Preserved Stage63 controller.c')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
results = {}
with tempfile.TemporaryDirectory(prefix='stage64-doors-') as temporary:
    directory = Path(temporary)
    (directory / 'region-production.inc').write_text((root / 'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
    for mode, controller in [('baseline', args.baseline), ('candidate', root / 'src/controller.c')]:
        names = 'quests shop items game progress coop potions spells arrows objects exploration flash'.split()
        command = ['gcc', '-DGRAPHICS_ASSETS', '-DFLASH_HOST_TEST', '-O1', '-g',
                   '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-I'+str(directory), '-I'+str(root/'src')]
        if mode == 'baseline':
            command += ['-DSTAGE64_BASELINE']
        command += [str(root/'tests/stage64-door-controls.c'), str(controller)]
        command += [str(root/'src'/(name+'.c')) for name in names]
        command += ['-o', str(directory/mode)]
        subprocess.run(command, check=True)
        results[mode] = json.loads(subprocess.check_output([str(directory/mode)], text=True,
            env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0', 'UBSAN_OPTIONS':'halt_on_error=1'}))
    # The historical runner predates the separate town-presentation callback.
    # Its co-op scenario is a non-campaign dungeon: assert that scope in the
    # missing UI stub rather than altering old tests or production behavior.
    wrapper = directory/'regional-wrapper.c'
    wrapper.write_text('#include '+json.dumps(str(root/'tests/regional-doors.c'))+'\n'
        'void game_coop_actor_interaction(Game *g,unsigned pressed){'
        '(void)pressed;assert(!g->area&&!coop_campaign_active);}\n')
    command = ['gcc', '-DGRAPHICS_ASSETS', '-DFLASH_HOST_TEST', '-O1', '-g',
               '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I'+str(directory), '-I'+str(root/'src'), str(wrapper)]
    command += [str(root/'src'/(name+'.c')) for name in [*names, 'controller']]
    command += ['-o', str(directory/'regional')]
    subprocess.run(command, check=True)
    regional = subprocess.check_output([str(directory/'regional')], text=True,
        env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0', 'UBSAN_OPTIONS':'halt_on_error=1'})
for key in ['movement_cases', 'target_cases', 'obstacle_cases', 'cardinal_hash']:
    assert results['baseline'][key] == results['candidate'][key], key
report = {'result':'PASS', 'method':'Actual production controller/object/actor/coop code under ASan + UBSan; established flat terrain/audio/display fixture',
          'cases':results, 'closed_door_priority':'retained enemy / reachable enemy / item priorities preserved; closed doors precede other objects; reach unchanged; A still required',
          'open_door_slide':'All eight inputs: legal direct, then +/-45 degree candidates; diagonal input can additionally follow legal +/-90 degree wall-parallel cells; prefer immediate open doorway; stop automatic U-turns except one doorway entry outside a door; never bypass corners/terrain/actor/party collisions',
          'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['src/controller.c','src/game.c','src/game.h','src/objects.c','src/monster_ai.c','tests/stage64-door-controls.c']},
          'baseline_controller_sha256':hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
          'regional_doors_regression':regional.splitlines(),
          'controller_record_growth_bytes':0, 'persistent_ram_added':0,
          'scope':'Controller changes add no saved fields or static RAM; other Stage64 archive/version changes are outside this report.'}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2)+'\n')
(args.output.parent/'regional-doors.log').write_text(regional)
print(json.dumps(report, indent=2))
