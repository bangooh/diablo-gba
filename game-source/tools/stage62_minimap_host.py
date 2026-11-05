"""ASan/UBSan test of actual doubled-map cache, packing and VRAM upload."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
root=Path(__file__).resolve().parents[1]
work=root/'build/stage62-minimap-host';work.mkdir(parents=True,exist_ok=True)
out=root/'generated/stage62-validation/minimap-host';out.mkdir(parents=True,exist_ok=True)
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
# Frozen baseline contours enlarged to nominal200% geometry in a
# deliberately oversized fixed world bitmap. Crop it independently, without
# candidate scrolling, packing, tile origins or fallback code.
base=Path('/workspace/scratch/eda30d53efcc/work/stage62/baseline/src/automap_raster.inc').read_text()
reference=base[:base.index('static UI_FN unsigned automap_pack(')]
reference=reference.replace('int ox=map_screen_fallback?120-map_round((game.x-game.y)*320,180):MAP_WORLD_X;', 'int ox=512;')
reference=reference.replace('int oy=map_screen_fallback?80-map_round((game.x+game.y)*320,362):MAP_WORLD_Y;', 'int oy=64;')
reference=reference.replace('unsigned width=map_screen_fallback?240:512,height=map_screen_fallback?160:256,stride=width/8;', 'unsigned width=1024,height=512,stride=128;')
reference=reference.replace('unsigned bytes=map_screen_fallback?4800:16384;', 'unsigned bytes=65536;')
reference=reference.replace('*320,180','*640,180').replace('*320,362','*640,360')
# Independent world-pixel oracle uses the exact rational line equation,
# not the candidate's integer intercept reduction or clipped viewport loop.
line_start=reference.index('static UI_FN void map_line(');line_end=reference.index('static UI_FN void automap_rasterize(',line_start)
reference=reference[:line_start]+r"""static UI_FN void map_line(int ax,int ay,int bx,int by){
 int u1=map_round(ax*16,9),v1=map_round(ay*16,9),u2=map_round(bx*16,9),v2=map_round(by*16,9);
 int begin=2*(u1-v1),end=2*(u2-v2);
 if(begin>end){int q=begin;begin=end;end=q;}
 for(int x=-512;x<512;x++)if(x>=begin&&x<=end){
  int dx=2*(u2-v2)-2*(u1-v1),dy=(u2+v2)-(u1+v1);
  double exact=(u1+v1-2)+(double)(x-2*(u1-v1))*dy/dx;
  int y=(int)exact;if(y>exact)y--;
  unsigned px=x+512,py=y+64;
  if(py<512)MAP_RASTER[(py/8*128+px/8)*8+(py&7)]|=1u<<(px&7);
 }
}
"""+reference[line_end:]
for name in ['automap_build_outline','automap_rasterize','map_edge','map_seen','map_line','map_flip_x','map_flip_y','MAP_GRID','MAP_EDGE_BYTES']:
 reference=re.sub(r'\b'+name+r'\b','reference_'+name,reference)
reference=reference.replace('MAP_RASTER','REF_RASTER');(work/'reference-raster.inc').write_text(reference)
ds=(root/'src/dungeon.c').read_text();start=ds.index('    if(dungeon_level>4)',ds.index('void game_generate_dungeon('));end=ds.index('\n',start)
ds=ds[:start]+'    if(dungeon_level>4){game_world_generate(g,seed);return;}'+ds[end:]
last='game_rooms_prepare_l1(g);game_dungeon_spawn(g);g->kills=0;population(g,&s,seed);if(room_x>=0)populate_butcher_room(g);game_wall_traps_generate(g,seed);'
assert ds.count(last)==1;ds=ds.replace(last,'game_dungeon_spawn(g);')
ds=ds.replace('#include "../generated/','#include "'+str(root/'generated')+'/');(work/'census-dungeon.inc').write_text(ds)
flags=['gcc','-DGRAPHICS_ASSETS','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation','-Wno-int-to-pointer-cast','-Wno-parentheses','-Wno-maybe-uninitialized','-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),'-I'+str(root/'src')]
objects=[]
for p in sorted((root/'src/worldgen').glob('*.c')):
 obj=work/(p.stem+'.o')
 if not obj.exists() or obj.stat().st_mtime<p.stat().st_mtime:subprocess.run([*flags,'-c',str(p),'-o',str(obj)],check=True)
 objects.append(str(obj))
subprocess.run([*flags,str(root/'tools/stage62_minimap_host.c'),str(root/'src/dungeon_visual.c'),*objects,'-o',str(work/'host')],check=True)
r=subprocess.run([str(work/'host'),str(work/'town.bin')],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=180)
(out/'stdout.jsonl').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr)
if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
report=json.loads(r.stdout.splitlines()[-1]);report.update(native_fallback_by_fog=json.loads(r.stdout.splitlines()[-2])['native_fallback_by_fog'],junction_diamonds=9216,negative_capacity_cases=report['unrepresentable_frames'],negative_capacity_behavior='Whole map hidden; no incomplete/stale map is published, retry only after position or fog change',sanitizers=['ASan','UBSan'],sourceSha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['src/automap_raster.inc','src/gba_mode0.c']},testedSnippetSha256={'mapConfigAndHelpers':hashlib.sha256(config.encode()).hexdigest(),'automapRaster':hashlib.sha256(source.encode()).hexdigest()},scope='Actual production draw/raster/pack/staging/upload; frozen Stage61 contour/fog merging with independent double-precision interpolation between coherent isometric vertices in a1024x512 oracle. 9216 isolated diamonds verify closed eight-connected perimeters and all shared endpoints. Native levels1–16×8seeds×4fog modes and boundary/central positions, town, dense synthetic maps. No ROM/FPS generated here.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
