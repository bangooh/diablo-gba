#!/usr/bin/env python3
"""Exercise production shrine/item/spell/floor behavior under ASan + UBSan."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,default=Path('/workspace/scratch/eda30d53efcc/work/stage61/shrine-host'));args=p.parse_args()
work=args.work;work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage61-validation/shrines';out.mkdir(parents=True,exist_ok=True)
watched=[root/'src'/name for name in ['environment_objects.inc','objects.c','spells.c','progress.c','exploration.c','game.h','spells.h','prop_archive.h'] if (root/'src'/name).exists()]+[root/'tests/environment-objects-stage61.c']
input_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
(work/'region-production.inc').write_text((root/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
names='quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()
flags=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O1','-g','-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections','-I'+str(work),'-I'+str(root/'src')]
objects=[]
for source in [root/'tests/environment-objects-stage61.c']+[root/'src'/f'{name}.c' for name in names]:
 obj=work/(source.stem+'.o');objects.append(str(obj));r=subprocess.run([*flags,'-c',str(source),'-o',str(obj)],text=True,capture_output=True)
 (work/(source.stem+'.compile.log')).write_text(r.stdout+r.stderr)
 if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
binary=work/'shrines';r=subprocess.run(['gcc','-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',*objects,'-o',str(binary)],text=True,capture_output=True)
(work/'link.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(binary)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=60)
(out/'host.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout.strip().splitlines()[-1]);report.update({'sanitizers':['address','undefined'],'production_sources_sha256':input_hashes,'limitations':['Hidden/Gloomy/Weird unsupported per-item persistent base modifications','Spooky/Tainted multiplayer-only variants not generated','Enchanted level zero becomes unavailable until a book is read (no separate known bit)','Spiritual uses port gold counter instead of physical inventory gold piles','Thaumaturgic re-seeds unopened ordinary chests too (compact prop state)']})
assert input_hashes=={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}, 'Source changed during run; rerun the focused gate after edits stop'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
