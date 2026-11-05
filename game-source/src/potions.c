#include "game.h"
static int restore(int maximum,uint32_t *roll){
    *roll^=*roll<<13;*roll^=*roll>>17;*roll^=*roll<<5;
    int quarter=maximum/4;if(quarter<1)quarter=1;
    return quarter/2+(int)(*roll%(unsigned)quarter);
}
/* Codes preserve the three original bottles and append full potions/elixirs. */
unsigned game_belt_code(unsigned kind){return kind>=ITEM_HEAL&&kind<=ITEM_REJUV?kind:kind>=ITEM_FULL_HEAL&&kind<=ITEM_ELIXIR_VITALITY?kind-ITEM_FULL_HEAL+4:0;}
unsigned game_belt_kind(const Game *g,unsigned slot){unsigned code=slot<10?(unsigned)(g->belt>>(slot*4))&15:0;return code<=3?code:code<=10?code-4+ITEM_FULL_HEAL:0;}
int game_belt_valid(uint64_t belt,int count){
 if(belt>>40||count<0||count>10)return 0;unsigned occupied=0;
 for(unsigned i=0;i<10;i++){unsigned code=(unsigned)(belt>>(i*4))&15;if(code>10)return 0;occupied+=code!=0;}
 return occupied==(unsigned)count;
}
void game_belt_remove(Game *g,unsigned slot){if(slot<10&&game_belt_kind(g,slot)){g->belt&=~((uint64_t)15<<(slot*4));g->potions--;}}
int game_add_potion(Game *g,unsigned kind){
 unsigned code=game_belt_code(kind);if(!code)return 0;
 for(unsigned i=0;i<10;i++)if(!game_belt_kind(g,i)){g->belt|=(uint64_t)code<<(i*4);g->potions++;return 1;}
 return 0;
}
int game_use_elixir(Game *g,unsigned kind){
 if(g->hp<=0||kind<ITEM_ELIXIR_STRENGTH||kind>ITEM_ELIXIR_VITALITY)return 0;
 unsigned stat=kind-ITEM_ELIXIR_STRENGTH,cls=game_hero_class();uint8_t *value=&hero_progress.strength+stat;
 /* Retail consumes at the cap too; it does not award a stat point or level. */
 if(*value<game_stat_limit(cls,stat)){
  ++*value;
  if(stat==1){unsigned delta=cls==2?128:64;hero_progress.max_mana_q6+=delta;}
  if(stat==3){unsigned delta=cls==0?128:64;hero_progress.max_hp_q6+=delta;}
  game_items_update(g);
 }return 1;
}
/* Shared exact-bottle effect for belt hotkeys and inventory UseInvItem. */
int game_drink_potion(Game *g,unsigned kind){
 unsigned health=kind==ITEM_HEAL||kind==ITEM_FULL_HEAL||kind==ITEM_REJUV||kind==ITEM_FULL_REJUV;
 unsigned mana=kind==ITEM_MANA||kind==ITEM_FULL_MANA||kind==ITEM_REJUV||kind==ITEM_FULL_REJUV;
 if(g->hp<=0||(!health&&!mana))return 0;
  /* Derive the roll from synchronized state, not a process-local RNG history. */
  uint32_t roll=item_seed(g->ticks^(uint32_t)g->belt^(uint32_t)(g->belt>>32)^((uint32_t)(uint16_t)g->hp<<16)^(uint16_t)g->mana)|1u;
  unsigned cls=game_hero_class();
  if(health){int maximum=game_max_hp();if(kind==ITEM_FULL_HEAL||kind==ITEM_FULL_REJUV)g->hp=maximum;
   else{g->hp+=restore(maximum,&roll)*(cls==0?4:cls==1?3:2)/2;if(g->hp>maximum)g->hp=maximum;}if(g->hp==maximum)hero_progress.life_steal_fraction=0;}
  if(mana){if(kind==ITEM_FULL_MANA||kind==ITEM_FULL_REJUV)game_restore_mana(g,0,1);
   else game_restore_mana(g,restore(g->max_mana,&roll)*(cls==2?4:cls==1?3:2)/2,0);}
 return kind;
}
int game_use_potion(Game *g,unsigned type){
 if(g->hp<=0||(type!=ITEM_HEAL&&type!=ITEM_MANA))return 0;
 for(unsigned i=0;i<10;i++){
  unsigned kind=game_belt_kind(g,i),health=kind==ITEM_HEAL||kind==ITEM_FULL_HEAL||kind==ITEM_REJUV||kind==ITEM_FULL_REJUV;
  unsigned mana=kind==ITEM_MANA||kind==ITEM_FULL_MANA||kind==ITEM_REJUV||kind==ITEM_FULL_REJUV;
  if(type==ITEM_HEAL?!health:!mana)continue;
  if(game_drink_potion(g,kind)){game_belt_remove(g,i);return kind;}
 }return 0;
}
