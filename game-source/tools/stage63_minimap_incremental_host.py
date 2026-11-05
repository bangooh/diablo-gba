"""Independent, stateful minimap discovery sequence checks against world edges."""
from pathlib import Path
import hashlib,json,os,subprocess,sys

root=Path(__file__).resolve().parents[1]
work=root/'build/stage63-minimap-incremental-host';work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage63-validation/minimap-incremental-host';out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(root/'tools'));from lz77 import decompress
(work/'town.bin').write_bytes(decompress((root/'generated/town_collision.lz').read_bytes()))
source=(root/'src/automap_raster.inc').read_text();main=(root/'src/gba_mode0.c').read_text()
def function(text,sig):
    start=text.index(sig);opening=text.index('{',start);end=opening+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
config=main[main.index('enum { MAP_TILES='):main.index('/* The information background starts')]
config+='\n'+'\n'.join(function(main,n) for n in [
    'static inline int map_round(', 'static inline int map_point_x(',
    'static inline int map_point_y(', 'static inline int map_origin_x(',
    'static inline int map_origin_y(', 'static UI_FN void automap_scroll(',
    'static UI_FN void automap_upload('])
commit=function(main,'static UI_FN void automap_commit(')
(work/'candidate-config.inc').write_text(config)
(work/'candidate-commit.inc').write_text(commit)
ds=(root/'src/dungeon.c').read_text()
start=ds.index('    if(dungeon_level>4)',ds.index('void game_generate_dungeon('));end=ds.index('\n',start)
ds=ds[:start]+'    if(dungeon_level>4){game_world_generate(g,seed);return;}'+ds[end:]
tail='game_rooms_prepare_l1(g);game_dungeon_spawn(g);g->kills=0;population(g,&s,seed);if(room_x>=0)populate_butcher_room(g);game_wall_traps_generate(g,seed);'
assert ds.count(tail)==1;ds=ds.replace(tail,'game_dungeon_spawn(g);')
ds=ds.replace('#include "../generated/','#include "'+str(root/'generated')+'/')
(work/'census-dungeon.inc').write_text(ds)
flags=['gcc','-DGRAPHICS_ASSETS','-O1','-g','-Wall','-Wextra',
       '-Wno-unused-function','-Wno-misleading-indentation','-Wno-int-to-pointer-cast',
       '-Wno-parentheses','-Wno-maybe-uninitialized','-fsanitize=address,undefined',
       '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
       '-I'+str(work),'-I'+str(root/'src')]
objects=[]
for p in sorted((root/'src/worldgen').glob('*.c')):
    obj=work/(p.stem+'.o')
    if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:
        subprocess.run([*flags,'-c',str(p),'-o',str(obj)],check=True)
    objects.append(str(obj))
# This gate checks semantics, not speed. O0 avoids spending minutes optimizing
# the independent oracle and included world generators on every fixture edit.
host_flags=['-O0' if f=='-O1' else f for f in flags]
subprocess.run([*host_flags,str(root/'tools/stage63_minimap_incremental_host.c'),
                str(root/'src/dungeon_visual.c'),*objects,'-o',str(work/'host')],check=True)
r=subprocess.run([str(work/'host'),str(work/'town.bin')],text=True,capture_output=True,
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=180)
(out/'stdout.jsonl').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout.splitlines()[-1])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report.update(sanitizers=['ASan','UBSan'],
    sourceSha256={p:sha(root/p) for p in ['src/automap_raster.inc','src/gba_mode0.c']},
    harnessSha256={p:sha(root/p) for p in ['tools/stage63_minimap_incremental_host.py','tools/stage63_minimap_incremental_host.c']},
    testedSnippetSha256={'mapConfigAndHelpers':hashlib.sha256(config.encode()).hexdigest(),
                        'automapCommit':hashlib.sha256(commit.encode()).hexdigest(),
                        'automapRaster':hashlib.sha256(source.encode()).hexdigest()},
    scope='Stateful production incremental/full raster, pack, prepare, DMA and commit with persistent mocked VRAM. Independent unit world-edge enumeration and floating line interpolation use fixed Stage62 projection equations, checking the entire cached ROI at its retained anchor. Includes all hardware flips, all palette banks/nonzero ink nibbles, branch/duplicate fog, shrink/reveal-all, cache recenter, modes0/2/1, atlas capacity, menu overwrite and hidden/late pending cancellation. No ROM or FPS claims.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
