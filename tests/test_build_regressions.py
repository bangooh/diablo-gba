"""0.9.7.2 build/runtime regressions that need no MPQ assets.

1. Every tool named by the regeneration scripts exists in game-source/tools.
2. Host-side decoders that compile src/actor_frames.c build with the host CC.
3. Dead-body raw-cache slots use the living actor raw-cache base.
4. Minimap/floor optimizations match the previous algorithms on random input.
"""
from pathlib import Path
import os, re, subprocess, tempfile

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
