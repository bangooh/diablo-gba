#!/usr/bin/env python3
"""Exhaustive3-byte prop bijection plus actual Flash/campaign-floor adapter."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,default=root/'build/stage64-prop-archive');a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage64-validation/prop-archive-host';out.mkdir(parents=True,exist_ok=True)
watched=['src/prop_archive.h','src/game.h','src/menu.inc','src/coop_campaign.inc','tests/stage64-prop-archive.c','tests/stage64-prop-archive-legacy.inc','tests/stage64-prop-flash.c']
def hashes():return {n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in watched}
before=hashes()
def function(source,name):
 start=source.index('static UI_FN int '+name+'(');end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
(a.work/'region-production.inc').write_text((root/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
(a.work/'campaign-floor-valid.inc').write_text(function((root/'src/coop_campaign.inc').read_text(),'campaign_floor_valid'))
flags=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O2','-g','-fsanitize=address,undefined','-fno-pie','-ffunction-sections','-fdata-sections','-I'+str(a.work),'-I'+str(root/'src')]
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
def run(cmd,log):
 r=subprocess.run(cmd,text=True,capture_output=True,env=env,timeout=120);(a.work/log).write_text(r.stdout+r.stderr)
 if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
 return r.stdout
exe=a.work/'codec';run([*flags,'-no-pie',str(root/'tests/stage64-prop-archive.c'),'-o',str(exe)],'codec-build.log')
codec=json.loads(run([str(exe)],'codec.log').strip().splitlines()[-1]);print(json.dumps(codec))
objects=[]
for src in [root/'tests/stage64-prop-flash.c']+[root/'src'/f'{n}.c' for n in 'quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()]:
 obj=a.work/(src.stem+'.o');objects.append(str(obj));run([*flags,'-c',str(src),'-o',str(obj)],src.stem+'.compile.log')
exe=a.work/'flash';run(['gcc','-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',*objects,'-o',str(exe)],'flash-link.log')
stdout=run([str(exe)],'flash.log');print(stdout);flash=json.loads(stdout.strip().splitlines()[-1])
assert before==hashes(),'Production inputs changed; rerun once frozen'
report={'result':'PASS','codec':codec,'flash':flash,'sanitizers':['address','undefined'],'source_sha256':before,'scope':'Exhaustive all96 representable kinds,80x80 coordinates and byte states; all16,777,216 serialized words; real single-player Flash and production incoming co-op floor validation, not physical link cable transport.'}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(out/'codec.log').write_text((a.work/'codec.log').read_text());(out/'flash.log').write_text(stdout)
