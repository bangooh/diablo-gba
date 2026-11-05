"""Compare actual candidate packing/upload pixels to the original screen raster."""
from pathlib import Path
import hashlib,json,os,re,subprocess
root=Path(__file__).resolve().parents[1]
candidate=Path('/workspace/scratch/eda30d53efcc/work/stage58/minimap')
work=Path('/workspace/scratch/eda30d53efcc/work/stage58/minimap-pixels');work.mkdir(parents=True,exist_ok=True)
census=work.parent/'minimap-census'
out=root/'generated/stage58-validation/minimap-pixels';out.mkdir(parents=True,exist_ok=True)
source=(candidate/'src/automap_raster.inc').read_text();(work/'candidate-raster.inc').write_text(source);(out/'candidate-raster.inc').write_text(source)
main=(candidate/'src/gba_mode0.c').read_text();config=main[main.index('enum { MAP_TILES='):main.index('/* The information background starts')]
(work/'candidate-config.inc').write_text(config);(out/'candidate-config.inc').write_text(config)
baseline=out/'original-automap.inc'
original=(baseline if baseline.exists() else root/'src/automap_raster.inc').read_text();baseline.write_text(original)
assert hashlib.sha256(original.encode()).hexdigest()=='6d26f0d2249a4c98c971e3764781cdcc943c75e95380cfc88ad1e70c0efc15fa'
reference=original[:original.index('static UI_FN void automap_draw(')]
names=['automap_build_outline','automap_rasterize','automap_pack','map_edge','map_seen','map_line','map_flip_x','map_flip_y','MAP_GRID','MAP_EDGE_BYTES']
for name in names:reference=re.sub(r'\b'+name+r'\b','reference_'+name,reference)
reference=reference.replace('MAP_RASTER','REF_RASTER');(work/'reference-raster.inc').write_text(reference)
flags=['gcc','-DGRAPHICS_ASSETS','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation','-Wno-int-to-pointer-cast','-Wno-parentheses','-Wno-maybe-uninitialized','-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),'-I'+str(census),'-I'+str(root/'src')]
objects=[str(census/(name+'.o')) for name in ['runtime','l2','l3','l4']]
subprocess.run([*flags,str(root/'tools/stage58_minimap_pixels.c'),str(root/'src/dungeon_visual.c'),*objects,'-o',str(work/'pixels')],check=True)
r=subprocess.run([str(work/'pixels'),str(census/'town.bin')],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'},timeout=90)
(out/'stdout.jsonl').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr);r.check_returncode()
report=json.loads(r.stdout.splitlines()[-1]);report.update(candidate_sha256=hashlib.sha256(source.encode()).hexdigest(),reference_sha256=hashlib.sha256(original.encode()).hexdigest(),sanitizers=['ASAN','UBSAN'],scope='Actual candidate automap_draw/raster/pack/upload staging; decoded BG2 flips, palette, tile references and wrapped world crop checked against original screen raster. Town and native levels1–16 two seeds, three fog modes; eight synthetic checker scales; boundaries and center. Geometry census fixture disables quest/actor population.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
