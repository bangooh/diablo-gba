from pathlib import Path
import subprocess,json,hashlib,sys
root=Path(__file__).resolve().parents[1];p=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'generated/stage56-validation/hud/oracles';p.mkdir(parents=True,exist_ok=True);src=root/'src';reference=root/'tests/stage56-hud-reference.c'
def function(path,name):
 s=path.read_text();a=s.index('static int '+name+'(');z=s.index('{',a);depth=1;e=z+1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
old=function(reference,'scroll_slot').replace('scroll_slot','reference_scroll_slot',1)
new=function(src/'spells.c','scroll_slot').replace('scroll_slot','candidate_scroll_slot',1)
s='''#include "src/coop.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
CoopPlayer coop_players[4];
unsigned coop_active,coop_id,coop_members,coop_turn,coop_ticks;
'''+old+'\n'+new+'''
static uint32_t random_state=0x51725901;
static unsigned next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static unsigned long checks;
static void check(unsigned spell){int a=reference_scroll_slot(spell),b=candidate_scroll_slot(spell);assert(a==b);checks++;}
int main(void){
 memset(&hero_inventory,0xff,sizeof(hero_inventory));
 Item *it=&hero_inventory.items[ITEM_SLOTS-1];
 for(unsigned kind=0;kind<256;kind++)for(unsigned flags=0;flags<256;flags++){
  it->kind=kind;it->flags=flags;
  for(unsigned spell=0;spell<128;spell++)check(spell);
  check(128);check(255);check(0xffffffffu);
 }
 for(unsigned trial=0;trial<10000;trial++){
  coop_active=trial%5!=0;coop_turn=(trial/5)%4;Inventory *bag=game_inventory();
  for(unsigned i=0;i<ITEM_SLOTS;i++){
   bag->items[i].kind=(next()%5?next()&255:next()&1?ITEM_BOOK_FIRE:ITEM_BOOK_HEAL);
   bag->items[i].flags=next()&255;
  }
  Inventory before=*bag;
  for(unsigned spell=0;spell<128;spell++)check(spell);
  assert(!memcmp(&before,bag,sizeof(before)));
 }
 printf("{\\"result\\":\\"PASS\\",\\"checks\\":%lu,\\"exhaustiveKindValues\\":256,\\"exhaustiveFlagValues\\":256,\\"allEncodableSpellIds\\":128,\\"additionalInvalidIds\\":[128,255,4294967295],\\"heterogeneousInventories\\":10000,\\"soloAndAllCoopInventoryOwners\\":true,\\"readOnlyInventory\\":true}\\n",checks);
}
'''
(p/'scroll-oracle.c').write_text(s)
flags=['-std=c11','-O2','-g','-ffunction-sections','-fdata-sections','-DGRAPHICS_ASSETS','-I'+str(root)]
subprocess.run(['gcc',*flags,'-c',str(src/'items.c'),'-o',str(p/'oracle-items.o')],check=True)
subprocess.run(['gcc',*flags,str(p/'scroll-oracle.c'),str(p/'oracle-items.o'),'-Wl,--gc-sections','-o',str(p/'scroll-oracle')],check=True)
r=json.loads(subprocess.check_output([str(p/'scroll-oracle')],text=True));r['referenceSourceSha256']=hashlib.sha256((reference).read_bytes()).hexdigest();r['candidateSourceSha256']=hashlib.sha256((src/'spells.c').read_bytes()).hexdigest();r['itemHelpersSourceSha256']=hashlib.sha256((src/'items.c').read_bytes()).hexdigest();r['method']='Extracted actual baseline and candidate scroll_slot functions; linked actual production items.c item_scroll/item_spell/item_is_spell/item_base and game_inventory, with real solo/co-op globals. No mirrored helper implementation.';(p/'scroll-oracle-report.json').write_text(json.dumps(r,indent=2)+'\n');print(r)

for name in ['scroll-oracle','oracle-items.o']:(p/name).unlink(missing_ok=True)
