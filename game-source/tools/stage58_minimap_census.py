"""Bounded host census for the proposed 512x256 world minimap dictionary."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--origin-x',type=int,default=256);parser.add_argument('--origin-y',type=int,default=48);parser.add_argument('--capacity',type=int,default=238);parser.add_argument('--name',default='baseline238');args=parser.parse_args()
work=Path('/workspace/scratch/eda30d53efcc/work/stage58/minimap-census');work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage58-validation/minimap-census'/args.name;out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'tools'));from lz77 import decompress
(work/'town.bin').write_bytes(decompress((root/'generated/town_collision.lz').read_bytes()))
source=(root/'src/automap_raster.inc').read_text();part=source[:source.index('static UI_FN unsigned automap_pack(')]
part=part.replace('int ox=120-map_round((game.x-game.y)*320,180);',f'int ox={args.origin_x};').replace('int oy=80-map_round((game.x+game.y)*320,362);',f'int oy={args.origin_y};')
part=part.replace('x>=240','x>=512').replace('end_x>=240','end_x>=512').replace('y>=160','y>=256').replace('end_y>=160','end_y>=256').replace('(unsigned)x<240','(unsigned)x<512').replace('(unsigned)y<160','(unsigned)y<256').replace('(unsigned)y/8*30','(unsigned)y/8*64').replace('i<4800','i<16384')
(work/'census-raster.inc').write_text(part)
ds=(root/'src/dungeon.c').read_text()
start=ds.index('    if(dungeon_level>4)',ds.index('void game_generate_dungeon('));end=ds.index('\n',start)
ds=ds[:start]+'    if(dungeon_level>4){game_world_generate(g,seed);return;}'+ds[end:]
last='game_rooms_prepare_l1(g);game_dungeon_spawn(g);g->kills=0;population(g,&s,seed);if(room_x>=0)populate_butcher_room(g);game_wall_traps_generate(g,seed);'
assert ds.count(last)==1
ds=ds.replace(last,'game_dungeon_spawn(g);')
ds=ds.replace('#include "../generated/','#include "'+str(root/'generated')+'/')
(work/'census-dungeon.inc').write_text(ds)
flags=['gcc','-DGRAPHICS_ASSETS','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation','-Wno-int-to-pointer-cast','-Wno-parentheses','-Wno-maybe-uninitialized','-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),'-I'+str(root/'src')]
objects=[]
for p in sorted((root/'src/worldgen').glob('*.c')):
 obj=work/(p.stem+'.o')
 if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:subprocess.run([*flags,'-c',str(p),'-o',str(obj)],check=True)
 objects.append(str(obj))
cmd=[*flags,str(root/'tools/stage58_minimap_census.c'),str(root/'src/dungeon_visual.c'),*objects,'-o',str(work/'census')]
subprocess.run(cmd,check=True);start=time.monotonic()
r=subprocess.run([str(work/'census'),str(work/'town.bin')],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=120)
(out/'scenes.jsonl').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr);r.check_returncode()
scenes=[json.loads(line) for line in r.stdout.splitlines()];max_scene=max(scenes,key=lambda s:s['tiles']);over=[s for s in scenes if s['tiles']>args.capacity]
groups={kind:{'scenes':len(a:=[s for s in scenes if s['kind']==kind]),'max':max(a,key=lambda s:s['tiles']),'overflows':sum(s['tiles']>args.capacity for s in a)} for kind in sorted(set(s['kind'] for s in scenes))}
report={'status':'census_complete','capacity':args.capacity,'scene_count':len(scenes),'max':max_scene,'overflow_count':len(over),'first_overflow':over[0] if over else None,'groups':groups,'seconds':round(time.monotonic()-start,3),'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'projection':{'x':f'{args.origin_x}+round((ax-ay)*320/180)','y':f'{args.origin_y}+round((ax+ay-1)*320/362)'},'semantics':'Native outline/fog/run merge/Bresenham; only projection origin and bounds changed. Includes zero tile. L1–4 native map generation stops before actor population; quests disabled.','production_changed':False,'rom_built':False}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
