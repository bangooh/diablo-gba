#!/usr/bin/env python3
"""Production fountain/theme interactions under ASan and UBSan."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,default=root/'build/stage64-environment-host');a=p.parse_args()
work=a.work;work.mkdir(parents=True,exist_ok=True);out=root/'generated/stage64-validation/environment-host';out.mkdir(parents=True,exist_ok=True)
watched=[root/'src'/n for n in ['environment_objects.inc','fountain_objects.inc','objects.c','spells.c','game.h','prop_archive.h','item_physical.inc']]+[root/'tests/environment-objects-stage64.c']
hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
(work/'region-production.inc').write_text((root/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
names='quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()
flags=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O1','-g','-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections','-I'+str(work),'-I'+str(root/'src')]
objects=[]
for source in [root/'tests/environment-objects-stage64.c']+[root/'src'/f'{name}.c' for name in names]:
 obj=work/(source.stem+'.o');objects.append(str(obj));r=subprocess.run([*flags,'-c',str(source),'-o',str(obj)],text=True,capture_output=True)
 (work/(source.stem+'.compile.log')).write_text(r.stdout+r.stderr)
 if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
binary=work/'environment';r=subprocess.run(['gcc','-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',*objects,'-o',str(binary)],text=True,capture_output=True)
(work/'link.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(binary)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=90)
(out/'host.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
assert hashes=={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched},'Sources changed during validation; rerun after freeze'
report=json.loads(r.stdout.strip().splitlines()[-1]);report.update({'sanitizers':['address','undefined'],'production_sources_sha256':hashes,'platform':'host production simulation with established display/terrain mocks'})
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
