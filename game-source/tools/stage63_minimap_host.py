"""ASan/UBSan test of actual doubled-map cache, packing and VRAM upload."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
root=Path(__file__).resolve().parents[1]
work=root/'build/stage63-minimap-host';work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage63-validation/minimap-host';out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'tools'));from lz77 import decompress
(work/'town.bin').write_bytes(decompress((root/'generated/town_collision.lz').read_bytes()))
source=(root/'src/automap_raster.inc').read_text();main=(root/'src/gba_mode0.c').read_text()
def function(text,sig):
 start=text.index(sig);opening=text.index('{',start);end=opening+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
config=main[main.index('enum { MAP_TILES='):main.index('/* The information background starts')]
config+='\n'+ '\n'.join(function(main,n) for n in ['static inline int map_round(', 'static inline int map_point_x(', 'static inline int map_point_y(', 'static inline int map_origin_x(', 'static inline int map_origin_y(', 'static UI_FN void automap_scroll(', 'static UI_FN void automap_upload('])
(work/'candidate-config.inc').write_text(config)
# Bundled frozen oracle is independent of candidate clipping, packing and scroll.
reference=(root/'tools/stage63_minimap_reference.inc').read_text()
(work/'reference-raster.inc').write_text(reference)
ds=(root/'src/dungeon.c').read_text();start=ds.index('    if(dungeon_level>4)',ds.index('void game_generate_dungeon('));end=ds.index('\n',start)
ds=ds[:start]+'    if(dungeon_level>4){game_world_generate(g,seed);return;}'+ds[end:]
last='game_rooms_prepare_l1(g);game_dungeon_spawn(g);g->kills=0;population(g,&s,seed);if(room_x>=0)populate_butcher_room(g);game_wall_traps_generate(g,seed);'
assert ds.count(last)==1;ds=ds.replace(last,'game_dungeon_spawn(g);')
ds=ds.replace('#include "../generated/','#include "'+str(root/'generated')+'/');(work/'census-dungeon.inc').write_text(ds)
flags=['gcc','-DGRAPHICS_ASSETS','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation','-Wno-int-to-pointer-cast','-Wno-parentheses','-Wno-maybe-uninitialized','-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),'-I'+str(root/'src'),'-I'+str(root/'tools')]
objects=[]
for p in sorted((root/'src/worldgen').glob('*.c')):
 obj=work/(p.stem+'.o')
 if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:subprocess.run([*flags,'-c',str(p),'-o',str(obj)],check=True)
 objects.append(str(obj))
subprocess.run([*flags,str(root/'tools/stage63_minimap_host.c'),str(root/'src/dungeon_visual.c'),*objects,'-o',str(work/'host')],check=True)
r=subprocess.run([str(work/'host'),str(work/'town.bin')],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=180)
(out/'stdout.jsonl').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout.splitlines()[-1]);report.update(native_fallback_by_fog=json.loads(r.stdout.splitlines()[-2])['native_fallback_by_fog'],junction_diamonds=9216,negative_capacity_cases=report['unrepresentable_frames'],negative_capacity_behavior='Whole map hidden; no incomplete/stale map is published, retry only after position or fog change',sanitizers=['ASan','UBSan'],sourceSha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['src/automap_raster.inc','src/gba_mode0.c']},oracleSha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['tools/stage63_minimap_reference.inc','tools/stage63_minimap_prepare_reference.inc']},testedSnippetSha256={'mapConfigAndHelpers':hashlib.sha256(config.encode()).hexdigest(),'automapRaster':hashlib.sha256(source.encode()).hexdigest()},scope='Actual production draw/raster/pack/staging/upload; frozen Stage61 contour/fog merging with independent double-precision interpolation between coherent isometric vertices in a1024x512 oracle. 9216 isolated diamonds verify closed eight-connected perimeters and all shared endpoints. Native levels1–16×8seeds×4fog modes and boundary/central positions, town, dense synthetic maps. No ROM/FPS generated here.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
