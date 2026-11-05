#!/usr/bin/env python3
"""Build real item/name fixtures and check wrapped row color boundaries."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.work.mkdir(parents=True,exist_ok=True);a.out.mkdir(parents=True,exist_ok=True)
(a.work/'region-production.inc').write_text((root/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
(a.work/'info-production.inc').write_text((root/'src/infopanel.inc').read_text().split('static UI_FN void info_monster(')[0])
names='quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()
flags=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O1','-g','-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections','-I'+str(a.work),'-I'+str(root/'src')]
objects=[]
for source in [root/'tests/item-colors-stage62.c']+[root/'src'/f'{n}.c' for n in names]:
 obj=a.work/(source.stem+'.o');objects.append(str(obj));subprocess.run([*flags,'-c',str(source),'-o',str(obj)],check=True)
binary=a.work/'item-colors';subprocess.run(['gcc','-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',*objects,'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
if r.returncode: print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout);report['source_sha256']={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in ['src/item_name_color.inc','src/infopanel.inc','src/items.c','src/item_magic.inc']}
(a.out/'fixtures.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'result':report['result'],'checks':report['checks'],'fixtures':len(report['fixtures'])}))
