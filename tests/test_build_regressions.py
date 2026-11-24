"""0.9.7.2 build/runtime regressions that need no MPQ assets.

1. Every tool named by the regeneration scripts exists in game-source/tools.
2. Host-side decoders that compile src/actor_frames.c build with the host CC.
3. Dead-body raw-cache slots use the living actor raw-cache base.
4. Minimap/floor optimizations match the previous algorithms on random input.
7. Options > Minimap shares the D-pad save word without disturbing old saves.
"""
from pathlib import Path
import json, os, re, subprocess, tempfile

r = Path(__file__).resolve().parents[1]
src = r / 'game-source/src'
cc = os.environ.get('CC', 'cc')

# 1. Missing pack_ui_audio.py stopped 0.9.7/0.9.7.1 generation.
names = set()
for script in sorted((r / 'scripts').glob('regenerate_*.py')):
    names.update(re.findall(r"tool\(\s*'([A-Za-z0-9_]+)'", script.read_text()))
missing = sorted(n for n in names if not (r / 'game-source/tools' / (n + '.py')).is_file())
assert not missing, f'regeneration tools missing from the tree: {missing}'
print(f'Regeneration tools present: {len(names)} PASS')

with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    # 2. verify_hero_pixels / pack_monster_pool compile these files with host gcc.
    subprocess.run([cc, '-shared', '-fPIC', '-O2', str(src / 'actor_frames.c'), str(src / 'lz.c'),
                    '-o', str(d / 'actor-codec.so')], check=True)
    print('Host actor decoder compiles PASS')

    # 4. Equivalence of the rewritten loops against the previous code.
    (d / 'eq.c').write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int old_floor(int n,int d){return n>=0?n/d:-((-n+d-1)/d);}
static int new_floor(int n,int d){if(d==2)return n>>1;if(d==4)return n>>2;if(d==8)return n>>3;return old_floor(n,d);}
/* Dirty scan: returns visited dirty bits and final cursor. */
static unsigned scan_old(const uint8_t *dirty,unsigned *cursor,unsigned limit,unsigned *out){
 unsigned visited=0,examined=0,k=0;
 while(visited++<1024&&examined<limit){
  unsigned n=*cursor;*cursor=(n+1)&1023;if(!(dirty[n>>3]&(1u<<(n&7))))continue;
  examined++;out[k++]=n;}
 return k;}
static unsigned scan_new(const uint8_t *dirty,unsigned *cursor,unsigned limit,unsigned *out){
 unsigned budget=1024,examined=0,k=0;
 while(budget&&examined<limit){
  unsigned n=*cursor,pending=dirty[n>>3]>>(n&7);
  if(!pending){unsigned step=8-(n&7);if(step>budget)step=budget;*cursor=(n+step)&1023;budget-=step;continue;}
  unsigned skip=(unsigned)__builtin_ctz(pending);
  if(skip>=budget){*cursor=(n+budget)&1023;budget=0;break;}
  n+=skip;budget-=skip+1;*cursor=(n+1)&1023;
  examined++;out[k++]=n;}
 return k;}
/* Edge run scan over one contiguous bit line. */
static unsigned runs_old(const uint8_t *e,unsigned base,int first,int last,int *out){
 unsigned k=0;for(int run=first;run<=last;run++){unsigned n=base+run;if((e[n>>3]>>(n&7))&1)out[k++]=run;}return k;}
static unsigned runs_new(const uint8_t *e,unsigned base,int first,int last,int *out){
 unsigned k=0;for(int run=first;run<=last;){unsigned n=base+(unsigned)run,p=e[n>>3]>>(n&7);
  if(!p){run+=8-(int)(n&7);continue;}run+=__builtin_ctz(p);if(run>last)break;out[k++]=run;run++;}return k;}
/* Floor drop culling: previous accept test versus early cull + final test. */
static int drop_old(int x,int y,int lift,int cx,int cy){
 int wx=1280+(x-y)*16-32,wy=72+(x+y)*8-64-lift;
 return !(wx+64<=cx||wx>=cx+248||wy+64<=cy||wy>=cy+168);}
static int drop_new(int x,int y,int lift,int cx,int cy){
 const int min_x=cx-64,max_x=cx+248,min_y=cy-64,max_y=cy+168;
 int wx=1280+(x-y)*16-32,wy=72+(x+y)*8-64;
 if(wx<=min_x||wx>=max_x||wy<=min_y||wy-10>=max_y)return 0;
 wy-=lift;return !(wy<=min_y||wy>=max_y);}
int main(void){
 for(int dv=2;dv<=8;dv*=2)for(int n=-200000;n<=200000;n++)assert(old_floor(n,dv)==new_floor(n,dv));
 srand(97);
 uint8_t dirty[128];unsigned a[1100],b[1100];
 for(int t=0;t<20000;t++){
  int density=rand()%5;for(int i=0;i<128;i++){unsigned v=0;for(int j=0;j<8;j++)if(density&&rand()%(1<<density)==0)v|=1u<<j;dirty[i]=(uint8_t)v;}
  unsigned c0=rand()&1023,c1=c0,lim=1+rand()%30;
  unsigned ka=scan_old(dirty,&c0,lim,a),kb=scan_new(dirty,&c1,lim,b);
  assert(ka==kb&&c0==c1);for(unsigned i=0;i<ka;i++)assert(a[i]==b[i]);
 }
 uint8_t edges[1180];int ra[128],rb[128];
 for(int t=0;t<20000;t++){
  for(int i=0;i<1180;i++)edges[i]=(uint8_t)(rand()%4==0?rand():0);
  unsigned fixed=rand()%81,base=fixed*97;int first=rand()%80,last=first+rand()%12;if(last>79)last=79;
  unsigned ka=runs_old(edges,base,first,last,ra),kb=runs_new(edges,base,first,last,rb);
  assert(ka==kb);for(unsigned i=0;i<ka;i++)assert(ra[i]==rb[i]);
 }
 for(int t=0;t<2000000;t++){
  int x=rand()%80,y=rand()%80,lift=rand()%2?10:0,cx=rand()%3000-500,cy=rand()%1500-300;
  assert(drop_old(x,y,lift,cx,cy)==drop_new(x,y,lift,cx,cy));
 }
 puts("Minimap scan, floor division and floor culling equivalence PASS");
 return 0;}
''')
    subprocess.run([cc, '-O2', str(d / 'eq.c'), '-o', str(d / 'eq')], check=True)
    subprocess.run([str(d / 'eq')], check=True)

# 3. RAW_DUNGEON_ART is rewritten to the full dungeon_art.bin size by
# pack_asset_sizes.py; using it here placed corpses over the hero stage.
death = (src / 'death_sprites.inc').read_text()
frames = (src / 'gba_mode0.c').read_text()
assert 'RAW_DUNGEON_ART' not in death, 'death sprites must not derive the raw cache from RAW_DUNGEON_ART'
assert death.count('town_map+DUNGEON_LAYOUT_OFFSET+MAP_W*MAP_H*2+(unsigned)cached*2048') == 2
assert 'town_map+DUNGEON_LAYOUT_OFFSET+MAP_W*MAP_H*2+cached*2048' in frames
print('Dead-body raw cache shares the living actor cache base PASS')

# 5. CPU party: ranged CPUs target what manual targeting can see, the rogue
# fires at its chosen enemy, and CPU input treats a final timer tick as ready
# (inputs are generated before game_step decrements move/attack/hit timers).
cpu = (src / 'coop_cpu.inc').read_text()
yield_ = (src / 'coop_cpu_yield.inc').read_text()
assert 'if(cls!=0&&!game_target_visible(g,i))continue;' in cpu
assert '{g->target=best;return KEY_A;}' in cpu and 'return best>=0&&cls==1?KEY_A:0;' not in cpu
ready = 'g->move_wait>1||g->attack_wait>1||g->hit_flash>1'
assert ready in cpu and ready in yield_
assert 'g->move_wait||g->attack_wait||g->hit_flash)' not in cpu + yield_
print('CPU party targeting and step cadence PASS')

# 6. Fixed wall stipple stops at solid wall mass. Floor beyond a thick block is
# another passage; counting it stippled near faces the player could not walk
# behind (0.9.7.3 and earlier). Both the cathedral and the L2-L4 paths agree.
frames_src = (src / 'gba_mode0.c').read_text()
assert frames_src.count('if(back&&!game.map[by][bx])break;') == 2
cath = frames_src[frames_src.index('static UI_FN void fixed_walls_prepare'):frames_src.index('static inline __attribute__((always_inline)) void fixed_world_wall_cell')]
world = frames_src[frames_src.index('void fixed_world_wall_cell'):]
world = world[:world.index('flags[cell]=')]
for body, mark in ((cath, 'flags|=0xc000'), (world, 'mask|=0xc0')):
    assert body.index('if(back&&!game.map[by][bx])break;') < body.index('if(back&&game.map[by][bx])' + mark)
print('Fixed wall stipple stops at solid wall mass PASS')

# 7. Options > Minimap (Right corner map / Center full map) is bit2 of the
# D-pad save word. Older saves (bit2 clear) keep their rotation and load as
# Right, the new default; Center keeps the rotation too.
opts = (src / 'options.h').read_text()
need = ('enum { OPTIONS_DPAD_TAG', 'enum { OPTIONS_DPAD_ORIGINAL', 'enum { OPTIONS_MAP_RIGHT',
        'static inline unsigned options_dpad_valid', 'static inline unsigned options_dpad_pack',
        'static inline unsigned options_dpad_unpack', 'static inline unsigned options_map_unpack')
lines = [l.replace('__attribute__((section(".text.ui"))) ', '') for l in opts.splitlines()
         if any(k in l for k in need)]
assert len(lines) == len(need), lines
option_test = r'''
int main(void){
 for(unsigned r=0;r<OPTIONS_DPAD_COUNT;r++){
  unsigned old=OPTIONS_DPAD_TAG|r;                      /* 0.9.7.4 and earlier */
  assert(options_dpad_unpack(old)==r&&options_map_unpack(old)==OPTIONS_MAP_RIGHT);
  for(unsigned m=0;m<OPTIONS_MAP_COUNT;m++){
   unsigned w=options_dpad_pack(r,m);
   assert(options_dpad_unpack(w)==r&&options_map_unpack(w)==m);
  }
 }
 unsigned bad[]={0u,0xffffffffu,OPTIONS_DPAD_TAG|3u,OPTIONS_DPAD_TAG|7u,OPTIONS_DPAD_TAG|8u};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++)
  assert(options_dpad_unpack(bad[i])==OPTIONS_DPAD_ORIGINAL&&options_map_unpack(bad[i])==OPTIONS_MAP_RIGHT);
 puts("Minimap layout option shares the D-pad save word PASS");return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    (d / 'opt.c').write_text('#include <assert.h>\n#include <stdio.h>\n' + '\n'.join(lines) + option_test)
    subprocess.run([cc, '-O2', str(d / 'opt.c'), '-o', str(d / 'opt')], check=True)
    subprocess.run([str(d / 'opt')], check=True)
corner = (src / 'automap_corner.inc').read_text()
assert 'MC_SIZE=64' in corner and 'R16(0x04000018)=(uint16_t)((-MC_BOX_X)&511)' in corner and 'if(map_corner()){mc_draw();return;}' in (src / 'automap_stream.inc').read_text()
assert 'if(map_corner()){mc_markers();return;}' in frames and 'if(map_corner()){mc_scroll();return;}' in frames
print('Right corner minimap uses fixed box tiles under a centred hero PASS')

# 8. Floors 1-4 fixed walls use the 0.9.7 75% stipple (odd row, odd column
# cleared), matching floors 5-16; the fast path's mask follows the same rule.
wall_tool = (r / 'game-source/tools/pack_fixed_wall_art.py').read_text()
assert 'not (p&1 and (p//8)&1)' in wall_tool and 'not (x&1 and y&1)' in wall_tool
assert '((p%8+p//8)&1)' not in wall_tool and '((x+y)&1)' not in wall_tool
stipple = frames[frames.index('void terrain_stipple('):]
assert 'unsigned mask=(p&2)?0x00ff00ffu:0xffffffffu;' in stipple[:600]
print('Fixed walls on floors 1-4 keep the 75% stipple PASS')

# 9. The town corner map drops one-cell obstacles and unreachable pockets
# from a generated list; the corner renderer only evaluates corner pixels.
town_tool = (r / 'game-source/tools/pack_town_minimap_stage80.py').read_text()
assert 'town_corner_cells' in town_tool and 'len(cells)<=8' in town_tool
assert 'town_corner_cells[i]' in corner and 'if(a==b&&b==c&&c==d)return 0;' in corner
assert 'mc_expand_table[row[r*8]]*ink' in corner
print('Town corner clean-up and corner-pixel fast paths PASS')

# 10. GOG's DIABDAT.MPQ re-encodes gendata\logo.smk. The native-input check
# and the opening-audio packer accept both known encodings.
native_map = json.loads((r / 'docs/native-input-map.json').read_text())
logo = [row for row in native_map if row['mpq'] == 'gendata\\logo.smk']
gog_logo = 'ff55c72cc092b129ec31b085e1a8db047caca201a2ca3bbcb48d4c079ac3b480'
assert len(logo) == 1 and logo[0].get('sha256_alternatives') == [gog_logo]
assert "row.get('sha256_alternatives', [])" in (r / 'scripts/restore_native_inputs.py').read_text()
opening_audio = (r / 'game-source/tools/pack_front_opening_audio.py').read_text()
assert gog_logo in opening_audio and 'SOURCE_SHAS[source_sha]' in opening_audio
print('GOG logo.smk encoding is accepted PASS')

# 11. Motion floors each axis's travelled distance, so on 2:1 screen
# diagonals Y never moves on a frame where X does not (all four directions).
adv = frames[frames.index('void advance(Motion *m){'):]
adv = adv[:adv.index('\n}\n')]
assert '&~255u' in adv and 'dx<0?-dx:dx' in adv and 'dy<0?-dy:dy' in adv
def step(d, e, t):
    p = (abs(d) * e // t) & ~255
    return -p if d < 0 else p
for sx, sy in ((16, -8), (-16, 8), (16, 8), (-16, -8)):
    for total in (18, 24, 27, 30, 36):
        prev = (0, 0)
        for e in range(1, total):
            x, y = step(sx * 256, e, total) >> 8, step(sy * 256, e, total) >> 8
            assert not (y != prev[1] and x == prev[0]), (sx, sy, total, e)
            prev = (x, y)
print('Diagonal motion moves Y only with X PASS')

# 13. Ranged targeting reaches as far below the hero as above it.
ctl = (src / 'controller.c').read_text()
assert 'dx+dy < -8||dx+dy > 8)return 0;' in ctl
print('Targeting range is symmetric above and below PASS')
