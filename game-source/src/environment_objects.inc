/* Stage61: retail AddShrine/OperateShrine and OperateBookCase, as preserved
 * in source_assets/item_catalog/objects.cpp. Prop identity and the existing
 * state byte are sufficient: no new per-floor or per-hero allocation.
 * Stage64's tagged physical item fields retain Hidden/Gloomy/Weird changes.
 * Spooky/Tainted are selected only in multiplayer; Hellfire variants are excluded.
 */
enum { ENV_NOTICE_FIRST=160,ENV_NOTICE_BOOKCASE=186 };
static int environment_is_shrine(unsigned k){return k==PROP_SHRINE_L||k==PROP_SHRINE_R;}
static uint32_t environment_seed(const Game *g,unsigned i){
 const Prop *p=&g->props[i];
 return item_seed(campaign_seed^dungeon_level*0x9e3779b9u^(p->x+p->y*80u)*0x45d9f3bu^0x5348524eu)|1u;
}
static int shrine_type_supported(unsigned type){
 return type<26&&(coop_active?(type!=2&&type!=3&&type!=8):(type!=18&&type!=25));
}
static const char *fountain_name(unsigned kind);
static const char *fountain_notice(unsigned notice);
unsigned game_shrine_type(const Game *g,unsigned i){
 if(i>=MAX_PROPS)return 26;
 unsigned kind=g->props[i].kind,random_shrine=kind==PROP_GOAT_SHRINE||kind==PROP_CAULDRON;
 if(!environment_is_shrine(kind)&&!random_shrine)return 26;
 uint32_t seed=environment_seed(g,i);unsigned type;
 do{type=random_n(&seed,26);}while(!shrine_type_supported(type)||(type==7&&game_dungeon_depth()>8)||(random_shrine&&type==8));
 return type;
}
const char *game_environment_name(const Game *g,unsigned i){
 static const char *const names[26]={
  "MYSTERIOUS SHRINE","HIDDEN SHRINE","GLOOMY SHRINE","WEIRD SHRINE","MAGICAL SHRINE","STONE SHRINE","RELIGIOUS SHRINE",
  "ENCHANTED SHRINE","THAUMATURGIC SHRINE","FASCINATING SHRINE","CRYPTIC SHRINE",
  "MAGICAL SHRINE","ELDRITCH SHRINE","EERIE SHRINE","DIVINE SHRINE","HOLY SHRINE",
  "SACRED SHRINE","SPIRITUAL SHRINE","SPOOKY SHRINE","ABANDONED SHRINE","CREEPY SHRINE","QUIET SHRINE",
  "SECLUDED SHRINE","ORNATE SHRINE","GLIMMERING SHRINE","TAINTED SHRINE"
 };
 if(i>=MAX_PROPS)return 0;
 if(g->props[i].kind==PROP_SLAIN_HERO)return "SLAIN HERO";
 if(g->props[i].kind==PROP_STORY_BOOK)return story_names76[game_story_index()];
 if(g->props[i].kind==PROP_FLAME_LEVER)return g->props[i].state?"FLAME LEVER - OFF":"FLAME LEVER - ON";
 if(g->props[i].kind>=PROP_BLOOD_FOUNTAIN)return fountain_name(g->props[i].kind);
 unsigned type=game_shrine_type(g,i);if(type<26)return names[type];
 return game_prop_is_bookcase(g->props[i].kind)?"BOOKCASE":fountain_name(g->props[i].kind);
}
const char *game_environment_notice(unsigned notice){
 static const char *const messages[26]={
  "ONE STAT +5, OTHERS -1","EQUIPPED DURABILITY CHANGED","ARMOR +2, WEAPON MAX -1","WEAPON MAX DAMAGE +1","MANA SHIELD GRANTED","ALL STAVES RECHARGED","ALL EQUIPMENT REPAIRED",
  "SPELL LEVELS REDISTRIBUTED","CHESTS REFILLED","FIREBOLT +2, MAX MANA -10%","NOVA AND FULL MANA",
  "MANA SHIELD GRANTED","POTIONS REJUVENATED","MAGIC +2","HEALTH AND MANA RESTORED","TELEPORTED",
  "CHARGED BOLT +2, MANA -10%","GOLD FROM EMPTY BAG SPACE","OTHER HEROES RESTORED","DEXTERITY +2","STRENGTH +2","VITALITY +2",
  "DUNGEON MAP REVEALED","HOLY BOLT +2, MANA -10%","ALL ITEMS IDENTIFIED","OTHER HEROES STATS CHANGED"
 };
 if(notice==ENV_NOTICE_BOOKCASE)return 0;
 return notice>=ENV_NOTICE_FIRST&&notice<ENV_NOTICE_FIRST+26?messages[notice-ENV_NOTICE_FIRST]:fountain_notice(notice);
}
/* Loot after a refill is seeded by the consumed shrine set rather than time,
 * interaction order, or which player operated it. Barrels must NOT use this
 * salt: their pre-reserved monster budget belongs to the generated floor. */
int game_shrine_chest_eligible(const Game *g,unsigned i){
 if(i>=MAX_PROPS)return 0;unsigned kind=g->props[i].kind;
 if(kind!=PROP_CHEST&&kind!=PROP_CHEST_MEDIUM&&kind!=PROP_CHEST_LARGE)return 0;
 /* Native OBJ_TCHEST is not reopened, even after its trap was disarmed/fired.
  * The port stores that type implicitly, so test the immutable identity. */
 return game_dungeon_depth()<=4||game_wall_trap_linked(g,i)||
  item_seed(campaign_seed^dungeon_level*0x9e3779b9u^(i+1)*0x74726170u)%100>=10;
}
uint32_t game_shrine_loot_salt(const Game *g){
 uint32_t salt=0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(environment_is_shrine(g->props[i].kind)&&g->props[i].state&&game_shrine_type(g,i)==8)
  salt^=environment_seed(g,i);
 return salt;
}
static void shrine_set_level(unsigned spell,unsigned level){
 if(spell<=2){hero_progress.spell_level[spell-1]=(uint8_t)level;return;}
 spell-=3;unsigned shift=(spell&1)*4;
 hero_progress.spell_extra[spell/2]=(hero_progress.spell_extra[spell/2]&~(15u<<shift))|(level<<shift);
}
static void shrine_stat(Game *g,unsigned stat,int amount){
 unsigned cls=game_hero_class();uint8_t *value=&hero_progress.strength+stat;
 int next=(int)*value+amount,limit=game_stat_limit(cls,stat);
 if(next<0)next=0;if(next>limit)next=limit;amount=next-*value;*value=(uint8_t)next;
 if(stat==1){int max=(int)hero_progress.max_mana_q6+amount*(cls==2?128:64);hero_progress.max_mana_q6=max<0?0:max>63999?63999:max;}
 if(stat==3){int max=(int)hero_progress.max_hp_q6+amount*(cls==0?128:64);hero_progress.max_hp_q6=max<64?64:max>63999?63999:max;}
 game_items_update(g);
}
static void shrine_spell_trade(Game *g,unsigned spell){
 game_spell_grant(spell);game_spell_grant(spell);
 unsigned loss=hero_progress.max_mana_q6/10;hero_progress.max_mana_q6-=loss;
 int mana=(int)g->mana*64+hero_progress.mana_fraction-(int)hero_progress.mana_debt*64-(int)loss;
 game_items_recalc();g->max_mana=item_effects.max_mana;
 if(mana<0||(item_effects.special_flags&ITEM_NOMANA))mana=0;if(mana>g->max_mana*64)mana=g->max_mana*64;
 g->mana=mana/64;hero_progress.mana_fraction=mana%64;hero_progress.mana_debt=0;
}
/* Reserve all positions and capacity before publishing any reward or consuming
 * the object. A full floor can be cleared and the same deterministic reward
 * claimed later; failed operations never repair/reward twice. */
static int environment_rewards(Game *g,unsigned i,const Item *items,unsigned count){
 unsigned free=0;int xs[2],ys[2];
 for(unsigned n=0;n<MAX_DROPS;n++)free+=!g->drops[n].active;
 if(free<count)goto full;
 unsigned found=0;const Prop *p=&g->props[i];
 for(int radius=0;radius<=3&&found<count;radius++)for(int y=-radius;y<=radius&&found<count;y++)for(int x=-radius;x<=radius&&found<count;x++){
  if(x!=-radius&&x!=radius&&y!=-radius&&y!=radius)continue;
  int xx=p->x+x,yy=p->y+y;if(!game_item_space(g,xx,yy))continue;
  xs[found]=xx;ys[found]=yy;found++;
 }
 if(found<count)goto full;
 for(unsigned n=0;n<count;n++){game_drop_put(g,item_to_drop(&items[n],xs[n],ys[n]));game_item_sound(g,item_sound_kind(&items[n]),0,xs[n],ys[n]);}
 return 1;
full:g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;
}
/* Source OperateShrine: the operator is unaffected; only other heroes
 * receive Spooky/Tainted. Execute within the acknowledged actor turn. */
static int shrine_other_heroes(Game *g,unsigned type,uint32_t seed){
 if(!coop_active||coop_turn>=4)return 0;
 unsigned actor=coop_turn;coop_capture(g,actor);
 for(unsigned p=0;p<4;p++)if(p!=actor&&(coop_members&(1u<<p))){
  coop_select(g,p);
  if(type==18){if(g->hp>0){g->hp=game_max_hp();hero_progress.life_steal_fraction=0;game_restore_mana(g,0,1);}}
  else {uint32_t personal=item_seed(seed^(p+1)*0x9e3779b9u);unsigned chosen=random_n(&personal,4);for(unsigned stat=0;stat<4;stat++)shrine_stat(g,stat,stat==chosen?1:-1);}
  g->pickup_notice=ENV_NOTICE_FIRST+type;g->pickup_notice_ticks=100;coop_capture(g,p);
 }
 coop_select(g,actor);return 1;
}
static int shrine_apply(Game *g,unsigned i,unsigned type,uint32_t seed){
 if(!shrine_type_supported(type))return 0;
 Inventory *bag=game_inventory();unsigned family=(game_dungeon_depth()+3)/4;
 if(!family)family=1;if(family>4)family=4;
 switch(type){
 case 18:case 25:return shrine_other_heroes(g,type,seed);
 case 0:{unsigned chosen=random_n(&seed,4);for(unsigned s=0;s<4;s++)shrine_stat(g,s,s==chosen?5:-1);break;}
 case 1:case 2:case 3:game_item_shrine(g,type,seed);break;
 case 4:case 11:if(!game_spell_shrine_effect(g,SPELL_SHIELD,2*family))return 0;break;
 case 5:for(unsigned n=0;n<ITEM_SLOTS;n++)if(item_is_staff(&bag->items[n]))bag->items[n].charges=bag->items[n].max_charges;break;
 case 6:for(unsigned n=0;n<ITEM_SLOTS;n++)if(bag->items[n].kind<ITEM_COUNT)item_set_durability(&bag->items[n],item_max_durability(&bag->items[n]));game_items_update(g);break;
 case 7:{unsigned count=0;for(unsigned s=1;s<=SPELL_MAX;s++)count+=game_spell_level(&hero_progress,s)!=0;
  if(count>1){unsigned chosen=random_n(&seed,count);for(unsigned s=1;s<=SPELL_MAX;s++)if(game_spell_level(&hero_progress,s)){
    unsigned old=game_spell_level(&hero_progress,s),next=old<15?old+1:15;
    if(!chosen)next=next>=2?next-2:0;
    chosen--;shrine_set_level(s,next);
  }}break;}
 case 8:for(unsigned n=0;n<MAX_PROPS;n++){Prop *p=&g->props[n];if(game_shrine_chest_eligible(g,n))p->state=0;}break;
 case 9:shrine_spell_trade(g,SPELL_FIREBOLT);break;
 case 10:if(!game_spell_shrine_effect(g,SPELL_NOVA,2*family))return 0;game_restore_mana(g,0,1);break;
 case 12:
  for(unsigned n=0;n<ITEM_SLOTS;n++){Item *it=&bag->items[n];unsigned k=it->kind;
   if(k==ITEM_HEAL||k==ITEM_MANA)it->kind=ITEM_REJUV;else if(k==ITEM_FULL_HEAL||k==ITEM_FULL_MANA)it->kind=ITEM_FULL_REJUV;
  }
  for(unsigned n=0;n<10;n++){unsigned k=game_belt_kind(g,n),next=0;
   if(k==ITEM_HEAL||k==ITEM_MANA)next=game_belt_code(ITEM_REJUV);else if(k==ITEM_FULL_HEAL||k==ITEM_FULL_MANA)next=game_belt_code(ITEM_FULL_REJUV);
   if(next)g->belt=(g->belt&~((uint64_t)15<<(n*4)))|((uint64_t)next<<(n*4));
  }break;
 case 13:shrine_stat(g,1,2);break;
 case 14:{Item rewards[2];unsigned kind=2*game_dungeon_depth()<7?ITEM_FULL_MANA:ITEM_FULL_REJUV;
  rewards[0]=item_create(kind,random_n(&seed,0x7fffffffu));rewards[1]=item_create(kind==ITEM_FULL_MANA?ITEM_FULL_HEAL:kind,random_n(&seed,0x7fffffffu));
  if(!environment_rewards(g,i,rewards,2))return 0;
  g->hp=game_max_hp();hero_progress.life_steal_fraction=0;game_restore_mana(g,0,1);break;}
 case 15:{unsigned found=0;
  /* OperateShrine's random whole-map target is discarded by AddRndTeleport:
   * ordinary levels phase +/-4..6 cells on both axes. Never cross an entire
   * dungeon or land on even a non-blocking prop; guard the original 500 tries. */
  for(unsigned n=0;n<500;n++){int dx=4+(int)random_n(&seed,3),dy=4+(int)random_n(&seed,3);
   if(random_n(&seed,2))dx=-dx;if(random_n(&seed,2))dy=-dy;
   int x=g->x+dx,y=g->y+dy;
   if(!game_walkable(g,x,y)||game_monster_blocks(g,x,y,-1)||coop_blocks(x,y,-1)||game_spell_golem_at(x,y,0))continue;
   unsigned occupied=0;for(unsigned j=0;j<MAX_PROPS;j++)occupied|=g->props[j].kind&&g->props[j].x==x&&g->props[j].y==y;
   if(occupied)continue;
   g->x=x;g->y=y;g->move_wait=0;g->attack_queued=0;g->target=-1;game_activate_nearby(g);found=1;break;
  }if(!found)return 0;break;}
 case 16:shrine_spell_trade(g,SPELL_CHARGED);break;
 case 17:{uint8_t grid[BAG_CELLS];items_grid(bag,grid);unsigned gold=0;
  for(unsigned n=0;n<BAG_CELLS;n++)if(grid[n]==ITEM_EMPTY)gold+=5*family+random_n(&seed,10*family);
  g->gold=gold>(unsigned)(GAME_GOLD_MAX-g->gold)?GAME_GOLD_MAX:g->gold+gold;break;}
 case 19:shrine_stat(g,2,2);break;
 case 20:shrine_stat(g,0,2);break;
 case 21:shrine_stat(g,3,2);break;
 case 22:game_explore_reveal();break;
 case 23:shrine_spell_trade(g,SPELL_HOLY);break;
 case 24:for(unsigned n=0;n<ITEM_SLOTS;n++)if(bag->items[n].enchant)bag->items[n].enchant|=0x8000;game_items_update(g);break;
 default:return 0;
 }
 return 1;
}
#include "fountain_objects.inc"
#include "environment_remaining.inc"
int game_environment_use(Game *g,int i){
 if(g->area||g->hp<=0||i<0||i>=MAX_PROPS||(g->props[i].state&&!game_prop_repeatable(g->props[i].kind)))return 0;
 if(g->props[i].kind>=PROP_SLAIN_HERO)return remaining_use(g,(unsigned)i);
 Prop *p=&g->props[i];uint32_t seed=environment_seed(g,(unsigned)i);unsigned notice;
 if(environment_is_shrine(p->kind)){
  unsigned type=game_shrine_type(g,(unsigned)i);if(!shrine_apply(g,(unsigned)i,type,seed))return 0;
  notice=ENV_NOTICE_FIRST+type;
 }else if(p->kind==PROP_BOOKCASE_L||p->kind==PROP_BOOKCASE_R){
  Item book=item_create(ITEM_BOOK_FIRE,seed);book.flags=(uint8_t)game_spell_book(seed,game_dungeon_depth());
  if(!environment_rewards(g,(unsigned)i,&book,1))return 0;notice=GAME_BOOK_NOTICE_BASE+item_spell(&book);
  game_quest_bookcase(g,p);
 }else return fountain_use(g,(unsigned)i,seed);
 p->state=environment_is_shrine(p->kind)?2:1;
 g->pickup_notice=(uint8_t)notice;g->pickup_notice_ticks=160;
 return 1;
}
