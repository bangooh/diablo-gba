#!/usr/bin/env python3
"""Exercise production shrine/item/spell/floor behavior under ASan + UBSan."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,default=root/'build/stage64-item-shrines');args=p.parse_args()
work=args.work;work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage64-validation/item-shrines-host';out.mkdir(parents=True,exist_ok=True)
watched=[root/'src'/name for name in ['items.h','items.c','item_magic.inc','item_physical.inc','item_effects.inc','infopanel.inc','shop_ui.inc','menu.inc','game.h','coop.c','shop.c'] if (root/'src'/name).exists()]+[root/'tests/item-shrines-stage64.c']
input_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
(work/'region-production.inc').write_text((root/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
names='quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()
flags=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O1','-g','-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections','-I'+str(work),'-I'+str(root/'src')]
objects=[]
for source in [root/'tests/item-shrines-stage64.c']+[root/'src'/f'{name}.c' for name in names]:
 obj=work/(source.stem+'.o');objects.append(str(obj));r=subprocess.run([*flags,'-c',str(source),'-o',str(obj)],text=True,capture_output=True)
 (work/(source.stem+'.compile.log')).write_text(r.stdout+r.stderr)
 if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
binary=work/'item-shrines';r=subprocess.run(['gcc','-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',*objects,'-o',str(binary)],text=True,capture_output=True)
(work/'link.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(binary)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=60)
(out/'host.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout.strip().splitlines()[-1]);report.update({'sanitizers':['address','undefined'],'production_sources_sha256':input_hashes,'bounds':['Signed base armor is preserved in-255..255 using nonstaffarmor bit7; weapon maximum damage saturates at255','Finite staff durability saturates at254; never wraps into staff255 indestructibility','Finite nonstaff max durability saturates at65534; legacy high-durability items remain valid'],'compatibility':'Untagged items, including old permanent repair loss, decode unchanged. Item/Drop stay16 bytes; old ROMs cannot read tagged physical items.'})
assert input_hashes=={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}, 'Source changed during run; rerun after edits stop'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
