"""Production shop flow/layout + authoritative purchase/coop regression checks."""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'generated/stage65-validation/shop-host'
OUT.mkdir(parents=True,exist_ok=True)
def function(s,signature):
 a=s.index(signature);b=s.index('{',a);n=1;i=b+1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
town=(ROOT/'src/town_dialogue.inc').read_text()
info=(ROOT/'src/infopanel.inc').read_text()
old=(ROOT/'tests/coop-shop.c').read_text()
(OUT/'region-production.inc').write_text((ROOT/'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
parts=['#include "item-fixture.inc"', '#include <stdlib.h>',
 'void game_generate_next(Game *g){(void)g;assert(0);}',
 'void game_coop_actor_interaction(Game *g,unsigned pressed){(void)g;(void)pressed;assert(!coop_campaign_active);}',
 'enum {HUD_WHITE=0,HUD_BLUE=1,HUD_GOLD=2,HUD_RED=3};',
 '#include "'+str(ROOT/'generated/town_dialogue_font.h')+'"',
 'typedef struct {const char *text;uint16_t length;uint8_t x,y,color,selected;} DialogueLine;',
 'static const char *const dialogue_talk_labels[9]={"Talk","Talk","Talk","Talk","Talk","Talk","Talk","Talk","Talk"};',
 'static void dialogue_topics_open(unsigned npc){(void)npc;menu_mode=17;}',
 function(town,'static UI_FN unsigned dialogue_glyph('),
 function(town,'static UI_FN unsigned dialogue_width('),
 function(town,'static UI_FN void dialogue_center('),
 info[:info.index('static UI_FN void info_render(')],
 function(info,'static UI_FN void info_item('),
 '#include "'+str(ROOT/'src/shop_ui.inc')+'"',
 old[old.index('typedef struct {Game game;'):old.index('static void local_ui_and_wirt(')],
 '#include "'+str(ROOT/'tests/stage65-shop-checks.inc')+'"']
(OUT/'driver.c').write_text('\n'.join(parts))
sources='quests shop items game progress coop potions spells arrows objects monster_ai controller exploration flash'.split()
exe=OUT/'driver'
cmd=['gcc','-DGRAPHICS_ASSETS','-DFLASH_HOST_TEST','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'tests'),str(OUT/'driver.c')]+[str(ROOT/'src'/f'{n}.c') for n in sources]+['-o',str(exe)]
subprocess.run(cmd,check=True)
run=subprocess.run([str(exe)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
(OUT/'checks.log').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr);run.check_returncode()
paths=list((ROOT/'src').glob('*'))+[ROOT/'tests/coop-shop.c',ROOT/'tests/item-fixture.inc',ROOT/'tests/stage65-shop-checks.inc',Path(__file__),ROOT/'generated/town_dialogue_font.h']
report={'result':'PASS','method':'ASAN/UBSAN production shop input/layout and unchanged authoritative transaction tests; terrain/audio are host stubs. Native ROM covers actual publication.', 'checks':run.stdout.splitlines(),'sourceSha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
