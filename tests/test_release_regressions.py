"""Run production hazard code without MPQ assets; check release UI/clock paths."""
from pathlib import Path
import os, re, shutil, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
 d=Path(td);shutil.copytree(r/'game-source/src',d/'src')
 (d/'generated').mkdir();(d/'generated/item_kinds.h').write_text('enum { ITEM_COUNT=256 };\n')
 (d/'generated/monster71_meta.h').write_text('enum { MONSTER_AI_BAT, MONSTER_AI_SNAKE, MONSTER_AI_SKELBOW, MONSTER_AI_GOATBOW }; static const unsigned char monster71_ai[242]={0};\n')
 exe=d/'hazards'
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-DGRAPHICS_ASSETS','-ffunction-sections','-fdata-sections','-I'+str(d/'src'),str(r/'tests/test_monster_hazards.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 # Compile actual renderer-facing clock conversion, independent of art assets.
 text=(r/'game-source/src/monster_ai.c').read_text()
 fn=text[text.index('unsigned game_enemy_special('):text.index('unsigned game_fallen_retreat(')]
 harness='''#include <assert.h>
#include <stdio.h>
typedef struct {unsigned kind;} Enemy;
typedef struct {Enemy enemies[1];} Game;
enum {MONSTER_AI_GARG=1, MONSTER_AI_MEGA=2};
unsigned special[1],rally[1],monster71_special_frames[1]={5},monster71_special_impact[1]={3};
unsigned game_monster_ai(unsigned kind){return MONSTER_AI_MEGA;}
unsigned game_monster_base_kind(unsigned kind){return kind;}
'''+fn+'''
int main(void){Game g={0};unsigned casts=0,marker=3,held=0;
 for(special[0]=19;special[0];){--special[0];unsigned frame=game_enemy_special(&g,0);
  if(frame==2){held++;if(marker==3){casts++;marker=0;}}
 }
 assert(casts==1 && held==15);puts("Balrog: one cast and14 additional held ticks PASS");}
'''
 (d/'clock.c').write_text(harness)
 subprocess.run([os.environ.get('CC','cc'),str(d/'clock.c'),'-o',str(d/'clock')],check=True)
 subprocess.run([str(d/'clock')],check=True)
ui=(r/'game-source/src/ui_panels.inc').read_text()
assert 'i<sizeof(right)/sizeof(right[0])' in ui
assert '"Resist light"' in ui
for name in ('frontend.inc','menu.inc'):
 s=(r/'game-source/src'/name).read_text()
 seed=re.search(r'game_seed_next\((.*?)\);',s).group(1)
 assert '0x04000100' in seed and 'campaign_seed' in seed and '0x0400010C' not in seed and '0x0400010c' not in seed
print('Character resistance row and both new-game seed entry paths PASS')
