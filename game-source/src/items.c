#include "game.h"
#include "coop.h"
#include "performance_options.h"
#if PERF_APPEARANCE_EVENTS
/* Raw cosmetic variants: never saved or included in the lockstep checksum. */
static uint8_t appearance_cache[4]={255,255,255,255};
#endif
/* Full retail base catalog and ordinary equipment bindings live in ROM. */
#include "../generated/item_catalog.inc"
#include "../generated/item_defs.inc"
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(BaseItemDef)==36,"compact ARM base item layout");
#endif
Inventory hero_inventory;
uint32_t item_sound_events[2];
volatile uint32_t hero_audio_event;
/* Local, transient drop-name event: resolve the live record by identity so
 * ground compaction and pickup cannot leave a stale item index. */
static struct {uint32_t seed;uint16_t tick;uint8_t kind,x,y,region;} drop_notice;
const Drop *game_drop_notice(const Game *g){
 if(drop_notice.region!=(g->area?0:dungeon_level)||(uint16_t)(g->ticks-drop_notice.tick)>=100)return 0;
 for(unsigned i=0;i<MAX_DROPS;i++){const Drop *d=&g->drops[i];if(d->active&&d->kind==drop_notice.kind&&d->seed==drop_notice.seed&&d->x==drop_notice.x&&d->y==drop_notice.y)return d;}
 return 0;
}
/* Capture the speaking class before co-op switches to its next actor. Audio
 * owns the cosmetic variation/cooldown; no saved state or gameplay RNG changes. */
__attribute__((section(".text.ui"),noinline)) void game_hero_error(const Game *g,unsigned reason){
 if(reason>=HERO_ERROR_COUNT||g->hp<=0||(coop_active&&coop_turn!=coop_id))return;
 hero_audio_event=1+game_hero_class()*HERO_ERROR_COUNT+reason;
}
/* Cosmetic local listener events never enter the lockstep state or save. */
void game_item_sound(const Game *g,unsigned kind,unsigned pickup,int x,int y){
 /* Gold pickup is silent; world drops still publish their falling cue. */
 if(pickup&&kind==ITEM_GOLD)return;
 int px=coop_active?coop_players[coop_id].x:g->x,py=coop_active?coop_players[coop_id].y:g->y;
 if(kind>ITEM_SOUND_GRAB||pickup>1||x-px<-6||x-px>6||y-py<-6||y-py>6)return;
 /* Inventory manipulation belongs to the local listener, not nearby peers. */
 if(pickup&&coop_active&&coop_turn!=coop_id)return;

 item_sound_events[pickup]=((item_sound_events[pickup]+256)&0xffffff00u)|kind;
}
int game_item_space(const Game *g,int x,int y){
 if(g->area){unsigned n=0;for(unsigned i=0;i<MAX_DROPS;i++)n+=!!g->drops[i].active;if(n>=TOWN_DROPS)return 0;}
 if(!game_walkable(g,x,y)||game_monster_blocks(g,x,y,-1))return 0;
 for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].x==x&&g->drops[i].y==y)return 0;
 return 1;
}
Inventory *game_inventory(void){return coop_active?&coop_players[coop_turn].hero.inventory:&hero_inventory;}
#define LIVE_INVENTORY (*game_inventory())
uint8_t item_damage_min,item_damage_max,item_armor,item_weapon_flags;
_Static_assert(sizeof(Item)==16,"persistent item record");
_Static_assert(sizeof(Drop)==16,"ground record carries charges and enchantment independently");
_Static_assert(ITEM_COUNT+32<ITEM_EMPTY,"item notifications fit in one byte");
int item_is_spell(const Item *it){return it->kind==ITEM_BOOK_FIRE||it->kind==ITEM_BOOK_HEAL;}
int item_is_staff(const Item *it){const BaseItemDef *base=item_base(it->kind);return base&&base->type==10;}
unsigned item_spell(const Item *it){return item_is_spell(it)?(it->flags?it->flags&127:it->kind-ITEM_BOOK_FIRE+1):item_is_staff(it)?it->flags&63:0;}
int item_scroll(const Item *it){return item_is_spell(it)&&(it->flags&128);}
unsigned item_magic(const Item *it){
 if(it->kind>=ITEM_COUNT)return 255;unsigned s=item_spell(it);
 if(item_unique(it))return item_defs[it->kind].magic;
 if(item_is_spell(it)&&game_spell_supported(s))return item_scroll(it)?spell_defs[s].magic:game_spell_requirement(s);
 if(item_is_staff(it)&&game_spell_supported(s))return it->flags&128?20:spell_defs[s].magic;
 return item_defs[it->kind].magic;
}
int item_valid(const Item *it){
 unsigned original=item_original_durability(it),physical=(it->flags&ITEM_PHYSICAL)&&!item_is_spell(it);
 const BaseItemDef *base=item_base(it->kind);
 if(it->reserved||(physical&&(!base||base->type<1||base->type>10||!original))||(!physical&&it->wear_loss&&(!original||original==ITEM_INDESTRUCTIBLE||it->wear_loss>=original)))return 0;
 if(physical&&(!item_max_durability(it)||(item_max_durability(it)==ITEM_INDESTRUCTIBLE&&original!=ITEM_INDESTRUCTIBLE)||(((base->type>=1&&base->type<=4)||base->type==10)&&item_weapon_high(it)<item_weapon_low(it))))return 0;
 if(it->kind<1||it->kind>=ITEM_COUNT||!item_enchant_valid(it)||item_durability(it)>item_max_durability(it)||(item_max_durability(it)==ITEM_INDESTRUCTIBLE&&item_durability(it)!=ITEM_INDESTRUCTIBLE))return 0;
 unsigned s=item_spell(it);
 if(item_is_staff(it)){
  if(item_unique(it)){
   unsigned spell=0,max=0;for(unsigned p=0;p<item_power_count(it);p++){ItemPower a=item_power_at(it,p);if(a.power==66){spell=a.low;max=a.high;}}
   return (it->flags&~ITEM_PHYSICAL)==spell&&it->max_charges<=max&&it->charges<=it->max_charges&&(!spell||game_spell_supported(spell));
  }
  if((it->flags&128)&&it->enchant)return 0;
  if(!(it->flags&~ITEM_PHYSICAL))return !it->charges&&!it->max_charges;
  if(!game_spell_supported(s)||!spell_defs[s].staff_level||it->charges>it->max_charges||it->max_charges>spell_defs[s].staff_max*item_charge_multiplier(it))return 0;
  return !(it->flags&128)||(it->kind==ITEM_STAFF&&s==SPELL_CHARGED&&it->max_charges<=40);
 }
 if(physical)return it->flags==ITEM_PHYSICAL||(base->type>=5&&base->type<=9&&it->flags==(ITEM_PHYSICAL|ITEM_PHYSICAL_NEGATIVE)&&it->charges);
 if(it->charges)return 0;
 return !it->flags||(item_is_spell(it)&&game_spell_supported(s)&&(item_scroll(it)||spell_defs[s].book));
}
int game_staff_slot(void){
 const Inventory *bag=game_inventory();unsigned usable=item_equipment_mask(bag,hero_progress.strength,hero_progress.magic,hero_progress.dexterity);
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->pos!=BAG_CELLS+EQUIP_HAND||!item_is_staff(it)||!item_valid(it)||!it->durability)continue;
  if(usable&(1u<<EQUIP_HAND))return i;
 }return -1;
}
uint32_t item_seed(uint32_t s){s^=s>>16;s*=0x7feb352du;s^=s>>15;s*=0x846ca68bu;return s^(s>>16);}
unsigned item_gold(uint32_t s){
 unsigned level=game_dungeon_depth();if(!level)level=1;
 level+=16*game_difficulty;
 unsigned value=5*level+item_seed(s)%(10*level);
 /* GetItemAttrs checks terrain type, not difficulty or effective depth.
  * The Vile Betrayer's depth15 fixed map uses Cathedral terrain. */
 if(dungeon_level>=13&&dungeon_level<=16)value+=value>>3;
 return value>5000?5000:value;
}
Item item_create_level(unsigned kind,uint32_t seed,unsigned level){
 if(kind>=ITEM_COUNT)kind=0;const ItemDef *d=&item_defs[kind];
 const BaseItemDef *base=item_base(kind);
 unsigned max_dur=base?base->durability:d->durability;
 unsigned dur=max_dur?max_dur/4+1+item_seed(seed^0x41u)%(max_dur/2):0;
 unsigned flags=kind==ITEM_BOOK_FIRE?game_spell_book(item_seed(seed),game_dungeon_depth()?game_dungeon_depth():1):0;
 if(kind==ITEM_BOOK_FIRE&&(item_seed(seed^0x87u)&3)==0){uint8_t choices[BASE_ITEM_COUNT];unsigned count=0;for(unsigned i=0;i<BASE_ITEM_COUNT;i++){const BaseItemDef *b=&base_item_defs[i];if((b->misc==21||b->misc==22)&&b->min_level<=2*game_dungeon_depth()&&game_spell_supported(b->spell))choices[count++]=b->spell;}if(count)flags=128|choices[item_seed(seed^0x99u)%count];}
 Item it={.seed=seed,.kind=(uint8_t)kind,.durability=(uint8_t)dur,.flags=(uint8_t)flags};
 /* Retail 1-in-4 non-spell branch uses regular staff affixes later. */
 if(item_is_staff(&it)&&(item_seed(seed^0x117u)&3)){
  unsigned s=game_spell_staff(item_seed(seed^0x118u),level,coop_active);
  it.flags=s;it.charges=it.max_charges=spell_defs[s].staff_min+item_seed(seed^0x119u)%(spell_defs[s].staff_max-spell_defs[s].staff_min+1);
 }return it;
}
Item item_create(unsigned kind,uint32_t seed){return item_create_level(kind,seed,2*(game_dungeon_depth()?game_dungeon_depth():1));}
void items_init(Inventory *bag,unsigned cls){
 for(unsigned i=0;i<ITEM_SLOTS;i++)bag->items[i]=(Item){.kind=ITEM_EMPTY};
 unsigned kind=cls==1?ITEM_SHORT_BOW:cls==2?ITEM_STAFF:ITEM_SHORT_SWORD;
 bag->items[0]=item_create(kind,1);bag->items[0].durability=item_defs[kind].durability;bag->items[0].pos=BAG_CELLS+EQUIP_HAND;
 if(cls==2){bag->items[0].flags=128|SPELL_CHARGED;bag->items[0].charges=bag->items[0].max_charges=40;}
 /* Release starter kit contains only the equipped class weapon. */
}
int items_grid(const Inventory *bag,uint8_t grid[BAG_CELLS]){
 for(unsigned i=0;i<BAG_CELLS;i++)grid[i]=ITEM_EMPTY;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->kind==ITEM_EMPTY)continue;
  if(!item_valid(it)||it->pos>=BAG_CELLS+EQUIP_COUNT)return 0;
  if(it->pos>=BAG_CELLS)continue;
  unsigned w=item_width(it),h=item_height(it),x=it->pos%10,y=it->pos/10;
  if(x+w>10||y+h>4)return 0;
  for(unsigned dy=0;dy<h;dy++)for(unsigned dx=0;dx<w;dx++){
   unsigned p=(y+dy)*10+x+dx;if(grid[p]!=ITEM_EMPTY)return 0;grid[p]=i;
  }
 }return 1;
}
int items_valid(const Inventory *bag){
 uint8_t grid[40];if(!items_grid(bag,grid))return 0;
 unsigned used=0,twohand=0;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->kind==ITEM_EMPTY||it->pos<BAG_CELLS)continue;
  unsigned s=it->pos-BAG_CELLS;const ItemDef *d=&item_defs[it->kind];
  if((s!=d->slot&&!(d->slot==EQUIP_RING1&&s==EQUIP_RING2))||(used&(1u<<s)))return 0;used|=1u<<s;
  if(item_flags(it)&ITEM_TWOHAND)twohand=1;
 }return !(twohand&&(used&(1u<<EQUIP_SHIELD)));
}
static int place(Inventory *bag,unsigned index){
 Item *it=&bag->items[index];uint8_t grid[40];if(!items_grid(bag,grid))return 0;
 unsigned w=item_width(it),h=item_height(it);
 for(unsigned y=0;y+h<=4;y++)for(unsigned x=0;x+w<=10;x++){
  int ok=1;for(unsigned dy=0;dy<h;dy++)for(unsigned dx=0;dx<w;dx++)if(grid[(y+dy)*10+x+dx]!=ITEM_EMPTY)ok=0;
  if(ok){it->pos=y*10+x;return 1;}
 }
 return 0;
}
int items_add(Inventory *bag,Item it){
 if(!item_valid(&it))return 0;
 for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_EMPTY){
  it.pos=BAG_CELLS;bag->items[i]=it;if(place(bag,i))return 1;
  bag->items[i]=(Item){.kind=ITEM_EMPTY};return 0;
 }
 return 0;
}
static void item_restore_effects(void);
static void game_item_values(void){
 int lo=item_effects.weapon_low,hi=item_effects.weapon_high;
 lo+=lo*item_effects.damage_percent/100+item_effects.damage_flat;hi+=hi*item_effects.damage_percent/100+item_effects.damage_flat;
 item_damage_min=lo>0?(lo<256?lo:255):1;item_damage_max=hi>item_damage_min?(hi<256?hi:255):item_damage_min;
 item_armor=item_effects.armor<0?0:item_effects.armor>255?255:item_effects.armor;item_weapon_flags=item_effects.weapon_flags;
}
void game_items_recalc(void){
#if PERF_APPEARANCE_EVENTS
 appearance_cache[coop_active?coop_turn:0]=255;
#endif
 item_build_effects(&hero_progress,game_inventory(),game_hero_class(),&item_effects);game_item_values();
}
void game_items_select(void){item_restore_effects();game_item_values();}
int game_uses_bow(void){return !!(item_weapon_flags&ITEM_BOW);}
unsigned game_armor(void){int ac=item_effects.armor+(int)game_effective_stat(2)/5;return ac>0?(unsigned)ac:0;}
/* N,U,S,D,B,M,H,T,A: weapon family, independent of catalog kind order. */
unsigned item_weapon_art(const Inventory *bag,unsigned strength,unsigned magic,unsigned dexterity){
 unsigned weapon=0,shield=0,usable=item_equipment_mask(bag,strength,magic,dexterity);
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];
  if(it->kind>=ITEM_COUNT||(it->pos!=BAG_CELLS+EQUIP_HAND&&it->pos!=BAG_CELLS+EQUIP_SHIELD))continue;
  if(!(usable&(1u<<(it->pos-BAG_CELLS))))continue;
  if(it->pos==BAG_CELLS+EQUIP_SHIELD){shield=1;continue;}
  /* Appearance sentinel; combat still uses the ordinary item effects. */
  if(item_unique(it)==&item_uniques[0])return 27;
  switch(item_base(it->kind)->type){
   case 1:weapon=2;break;case 2:weapon=8;break;case 3:weapon=4;break;
   case 4:weapon=5;break;case 10:weapon=7;break;default:break;
  }
 }
 return shield&&(weapon==0||weapon==2||weapon==5)?weapon+1:weapon;
}
/* Original CalcPlrInv: only a usable chest item changes the L/M/H body. */
unsigned item_armor_art(const Inventory *bag,unsigned strength,unsigned magic,unsigned dexterity){
 unsigned usable=item_equipment_mask(bag,strength,magic,dexterity);
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];
  if(it->kind>=ITEM_COUNT||it->pos!=BAG_CELLS+EQUIP_CHEST)continue;
  const ItemDef *d=&item_defs[it->kind];
  if(d->slot!=EQUIP_CHEST||!(usable&(1u<<EQUIP_CHEST)))continue;
  unsigned type=item_base(it->kind)->type;
  return type==8?1:type==9?2:0;
 }
 return 0;
}
/* Render appearance from the same exact equipment cache used for combat.
 * Resolve usability once, preserving the original weapon/shield/armor rules. */
unsigned item_hero_appearance(const Inventory *bag,const HeroProgress *stats,unsigned cls){
 ItemEffects effects;item_build_effects(stats,bag,cls,&effects);
 unsigned weapon=0,shield=0,armor=0,cleaver=0,chest_seen=0;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT)continue;
  unsigned slot=it->pos;
  if(slot==BAG_CELLS+EQUIP_CHEST&&!chest_seen){
   chest_seen=1;const ItemDef *d=&item_defs[it->kind];
   if(d->slot==EQUIP_CHEST&&(effects.usable&(1u<<EQUIP_CHEST))){unsigned type=item_base(it->kind)->type;armor=type==8?1:type==9?2:0;}
  }
  if(slot!=BAG_CELLS+EQUIP_HAND&&slot!=BAG_CELLS+EQUIP_SHIELD)continue;
  if(!(effects.usable&(1u<<(slot-BAG_CELLS))))continue;
  if(slot==BAG_CELLS+EQUIP_SHIELD){shield=1;continue;}
  if(item_unique(it)==&item_uniques[0])cleaver=1;
  switch(item_base(it->kind)->type){case 1:weapon=2;break;case 2:weapon=8;break;case 3:weapon=4;break;case 4:weapon=5;break;case 10:weapon=7;break;default:break;}
 }
 if(cleaver)return 27;
 if(shield&&(weapon==0||weapon==2||weapon==5))weapon++;
 return armor*9+weapon;
}
/* Equipment/stat mutations use game_items_recalc; selecting a co-op actor
 * uses game_items_select and leaves these independent cosmetic entries intact.
 * Scene replacement invalidates all owners before the first draw. */
void item_appearance_reset(void){
#if PERF_APPEARANCE_EVENTS
 for(unsigned i=0;i<4;i++)appearance_cache[i]=255;
#endif
}
unsigned item_render_appearance(const Inventory *bag,const HeroProgress *stats,unsigned cls){
 unsigned owner=4;
#if PERF_APPEARANCE_EVENTS
 if(coop_active){for(unsigned i=0;i<4;i++)if(bag==&coop_players[i].hero.inventory){owner=i;break;}}
 else if(bag==&hero_inventory)owner=0;
 if(owner<4&&appearance_cache[owner]!=255)return appearance_cache[owner];
#endif
 unsigned variant;
 if(coop_active)variant=item_hero_appearance(bag,stats,cls);
 else{unsigned weapon=item_weapon_art(bag,stats->strength,stats->magic,stats->dexterity);variant=weapon==27?27:item_armor_art(bag,stats->strength,stats->magic,stats->dexterity)*9+weapon;}
#if PERF_APPEARANCE_EVENTS
 if(owner<4)appearance_cache[owner]=(uint8_t)variant;
#endif
 return variant;
}
static __attribute__((section(".text.ui"))) int game_drop_position(const Game *g,int *x,int *y){
 unsigned count=0;for(unsigned i=0;i<MAX_DROPS;i++)count+=!!g->drops[i].active;
 if(count>=(g->area?TOWN_DROPS:MAX_DROPS))return 0;
 for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
  if(!dx&&!dy)continue;int xx=g->x+dx,yy=g->y+dy;
  if(!game_item_space(g,xx,yy)||coop_blocks(xx,yy,-1))continue;
  if(dx&&dy&&(!game_walkable(g,g->x+dx,g->y)||!game_walkable(g,g->x,g->y+dy)))continue;
  *x=xx;*y=yy;return 1;
 }
 return 0;
}
unsigned item_gold_amount(const Item *it){return it->flags&1?it->enchant:item_gold(it->seed);}
int __attribute__((section(".text.ui"))) game_gold_drop(Game *g,unsigned amount){
 if(g->hp<=0)return INV_BUSY;
 if(!amount||amount>5000||amount%100||amount>(unsigned)g->gold)return INV_REQUIREMENTS;
 int x,y;if(!game_drop_position(g,&x,&y))return INV_GROUND_FULL;
 Drop d={.x=x,.y=y,.active=1,.kind=ITEM_GOLD,.seed=item_seed(campaign_seed^g->ticks^g->gold),.flags=1,.enchant=amount};
 if(game_drop_put(g,d)<0)return INV_GROUND_FULL;
 g->gold-=amount;game_item_sound(g,ITEM_GOLD,0,x,y);return INV_OK;
}
int game_item_action(Game *g,unsigned index,int drop){
 game_items_recalc();
 unsigned belt_slot=index>=ITEM_SLOTS?index-ITEM_SLOTS:10;
 if(index>=ITEM_SLOTS+10)return 0;
 unsigned belt_kind=game_belt_kind(g,belt_slot);
 if(index>=ITEM_SLOTS?!belt_kind:LIVE_INVENTORY.items[index].kind>=ITEM_COUNT)return 0;
 if(g->hp<=0)return INV_BUSY;
 Item it=index>=ITEM_SLOTS?item_create(belt_kind,1):LIVE_INVENTORY.items[index];const ItemDef *d=&item_defs[it.kind];
 if(drop){
  int x,y;if(!game_drop_position(g,&x,&y))return INV_GROUND_FULL;
  if(game_drop_put(g,item_to_drop(&it,x,y))<0)return INV_GROUND_FULL;
  game_item_sound(g,item_sound_kind(&it),0,x,y);
  if(belt_slot<10){game_belt_remove(g,belt_slot);}else LIVE_INVENTORY.items[index]=(Item){.kind=ITEM_EMPTY};game_spell_cancel(g);g->attack_wait=0;g->attack_duration=0;g->attack_queued=0;g->swing_target=-1;game_items_update(g);return INV_OK;
 }
 if(it.kind==ITEM_SPECTRAL_ELIXIR){
  if(!game_campaign_layout()||quest_progress.state[1]!=3||quest_progress.stage[1]!=7)return INV_REQUIREMENTS;
  for(unsigned n=0;n<3;n++)for(unsigned k=ITEM_ELIXIR_STRENGTH;k<=ITEM_ELIXIR_VITALITY;k++)game_use_elixir(g,k);
  LIVE_INVENTORY.items[index]=(Item){.kind=ITEM_EMPTY};quest_progress.stage[1]=8;return INV_OK;
 }
 if(it.kind>=ITEM_ELIXIR_STRENGTH&&it.kind<=ITEM_ELIXIR_VITALITY){
  if(!game_use_elixir(g,it.kind))return INV_BUSY;
  if(belt_slot<10)game_belt_remove(g,belt_slot);else LIVE_INVENTORY.items[index]=(Item){.kind=ITEM_EMPTY};return INV_OK;
 }
 if(game_belt_code(it.kind)){
  if(!game_drink_potion(g,it.kind))return INV_REQUIREMENTS;
  if(belt_slot<10)game_belt_remove(g,belt_slot);else LIVE_INVENTORY.items[index]=(Item){.kind=ITEM_EMPTY};
  game_item_sound(g,it.kind,1,g->x,g->y);return INV_OK;
 }
 if(item_base(it.kind)->item_class==5)return INV_REQUIREMENTS;
 if(it.kind==ITEM_SCROLL_IDENTIFY)return INV_NO_IDENTIFY;
 if(item_is_spell(&it)){
  if(item_scroll(&it))return game_spell_request_scroll(g,index)?INV_OK:INV_REQUIREMENTS;
  if(!game_spell_learn(g,item_spell(&it)))return INV_REQUIREMENTS;
  LIVE_INVENTORY.items[index]=(Item){.kind=ITEM_EMPTY};return INV_OK;
 }
 Inventory staged=LIVE_INVENTORY;
 if(it.pos>=BAG_CELLS){if(!place(&staged,index))return INV_FULL;}
 else{
  if(game_effective_stat(0)<item_strength(&it)||game_effective_stat(1)<item_magic(&it)||game_effective_stat(2)<d->dexterity)return INV_REQUIREMENTS;
  unsigned slot=d->slot;
  if(slot==EQUIP_RING1){
   unsigned occupied=0;
   for(unsigned i=0;i<ITEM_SLOTS;i++)if(staged.items[i].kind<ITEM_COUNT&&staged.items[i].pos==BAG_CELLS+EQUIP_RING1)occupied=1;
   if(occupied){slot=EQUIP_RING2;for(unsigned i=0;i<ITEM_SLOTS;i++)if(staged.items[i].kind<ITEM_COUNT&&staged.items[i].pos==BAG_CELLS+EQUIP_RING2){slot=EQUIP_RING1;break;}}
  }
  staged.items[index].pos=BAG_CELLS+slot;
  for(unsigned i=0;i<ITEM_SLOTS;i++)if(i!=index){
   Item *old=&staged.items[i];if(old->kind>=ITEM_COUNT||old->pos<BAG_CELLS)continue;
   if(old->pos==BAG_CELLS+slot||((item_flags(&it)&ITEM_TWOHAND)&&old->pos==BAG_CELLS+EQUIP_SHIELD)||(slot==EQUIP_SHIELD&&(item_flags(old)&ITEM_TWOHAND))){
    if(!place(&staged,i))return INV_FULL;
   }
  }
 }
 if(!items_valid(&staged))return INV_FULL;
 LIVE_INVENTORY=staged;game_spell_cancel(g);g->attack_wait=0;g->attack_duration=0;g->attack_queued=0;g->swing_target=-1;game_items_update(g);game_item_sound(g,item_sound_kind(&it),1,g->x,g->y);return INV_OK;
}
#include "item_drag.inc"
int game_item_pickup(Game *g,int index){
 if(index<0||index>=MAX_DROPS)return 0;Drop *d=&g->drops[index];if(!d->active||d->kind>=ITEM_COUNT)return 0;
 if(d->kind==ITEM_GOLD){Item gold=item_from_drop(d);unsigned amount=item_gold_amount(&gold),room=GAME_GOLD_MAX-g->gold;
  if(!room)return 0;
  if(amount>room){g->gold=GAME_GOLD_MAX;d->flags=1;d->enchant=amount-room;game_item_sound(g,ITEM_GOLD,1,d->x,d->y);return 1;}
  g->gold+=amount;
 }
 else if(game_belt_code(d->kind)){if(!game_add_potion(g,d->kind)&&!items_add(&LIVE_INVENTORY,item_create(d->kind,d->seed))){g->pickup_notice=8;g->pickup_notice_ticks=60;game_hero_error(g,HERO_ERROR_SPACE);return 0;}}
 else if(!items_add(&LIVE_INVENTORY,item_from_drop(d))){
  g->pickup_notice=7;g->pickup_notice_ticks=60;game_hero_error(g,HERO_ERROR_SPACE);return 0;
 }
 g->pickup_notice=d->kind<4?d->kind+1:32+d->kind;g->pickup_notice_ticks=60;
 /* GBA direct pickup follows DevilutionX's grab cue. Keep the established
  * gold cue; placement/equipment uses the separate native inventory table. */
 game_item_sound(g,d->kind==ITEM_GOLD?ITEM_GOLD:ITEM_SOUND_GRAB,1,d->x,d->y);
 game_quest_picked(g,d);*d=(Drop){0};return 1;
}
unsigned item_candidate(uint32_t seed,unsigned level){
 unsigned count=0;for(unsigned i=1;i<ITEM_COUNT;i++)if(item_defs[i].level<=level)count+=item_defs[i].weight;
 if(!count)return 0;unsigned pick=item_seed(seed)%count;
 for(unsigned i=1;i<ITEM_COUNT;i++)if(item_defs[i].level<=level){if(pick<item_defs[i].weight)return i;pick-=item_defs[i].weight;}
 return 0;
}
int __attribute__((section(".text.ui"))) game_drop_put(Game *g,Drop drop){
 unsigned count=0;for(unsigned i=0;i<MAX_DROPS;i++)count+=!!g->drops[i].active;
 if(count>=(g->area?TOWN_DROPS:MAX_DROPS)||!drop.active)return -1;
 if(drop.kind==ITEM_GOLD&&!(drop.flags&1)){drop.flags=1;drop.enchant=item_gold(drop.seed);}
 unsigned n=0;for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active)g->drops[n++]=g->drops[i];
 int px=coop_active?coop_players[coop_id].x:g->x,py=coop_active?coop_players[coop_id].y:g->y;
 if((int)drop.x-px>=-6&&(int)drop.x-px<=6&&(int)drop.y-py>=-6&&(int)drop.y-py<=6){drop_notice.seed=drop.seed;drop_notice.kind=drop.kind;drop_notice.x=drop.x;drop_notice.y=drop.y;drop_notice.tick=g->ticks;drop_notice.region=g->area?0:dungeon_level;}
 g->drops[n]=drop;for(unsigned i=n+1;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};
 return n;
}
#include "item_magic.inc"
/* RndUItem gives each eligible native base row one ticket, including all four
 * book rows; IDROP_DOUBLE does not double its weight. Book aliases collapse to
 * the port's spell-bearing book kind, never to its random scroll branch. */
static __attribute__((section(".text.ui"),noinline)) unsigned unique_drop_binding(unsigned base){
 if(base_item_defs[base].misc==24)return ITEM_BOOK_FIRE;
 for(unsigned k=1;k<ITEM_COUNT;k++)if(item_base(k)==&base_item_defs[base]&&item_defs[k].weight)return k;
 return ITEM_EMPTY;
}
static __attribute__((section(".text.ui"),noinline)) unsigned unique_drop_candidate(uint32_t seed,unsigned level){
 unsigned count=0,pick=0;
 for(unsigned pass=0;pass<2;pass++){
  if(pass){if(!count)return ITEM_EMPTY;pick=item_seed(seed)%count;}
  for(unsigned b=0;b<BASE_ITEM_COUNT;b++){
   const BaseItemDef *base=&base_item_defs[b];
   /* Retail's book override follows the level/drop checks, so all four book
    * rows remain candidates even below their individual base minimum. */
   int eligible=base->misc==24||(base->drop_rate&&base->min_level<=level&&base->type!=0&&base->type!=11&&base->type!=14);
   if(!eligible||(!coop_active&&(base->spell==SPELL_RESURRECT||base->spell==SPELL_HEAL_OTHER)))continue;
   unsigned kind=unique_drop_binding(b);if(kind==ITEM_EMPTY)continue;
   if(!pass)count++;else if(!pick--)return kind;
  }
 }
 return ITEM_EMPTY;
}
/* SpawnItem -> SetupAllItems(uper=15, onlygood=TRUE). Preserve the established
 * seeded GBA rolls and supported powers. CheckUnique's <=15 gate is sixteen
 * values out of100, and its final eligible native row wins. Do not run the
 * ordinary <=1 unique roll again after this roll fails. */
static __attribute__((section(".text.ui"),noinline)) Item unique_drop_item(unsigned kind,uint32_t seed,unsigned base_level){
 unsigned level=base_level+4;
 Item it=item_create_level(kind,seed,level);
 if(item_is_spell(&it)){it.flags=game_spell_book(item_seed(seed),base_level/2?base_level/2:1);return it;}
 unsigned type=magic_type(&it);if(!type)return it;
 uint32_t state=it.seed^0xd1ab109u;
 if(magic_roll(&state,100)<=15){
  const BaseItemDef *base=item_base(kind);int chosen=-1;
  /* Quest-only rows0..8 retain their existing explicit reward paths. */
  for(unsigned u=9;u<ITEM_UNIQUE_COUNT;u++)if(item_unique_supported(u)&&item_uniques[u].base==base->unique_base&&item_uniques[u].min_level<=level)chosen=(int)u;
  if(chosen>=0&&item_make_unique(&it,(unsigned)chosen))return it;
 }
 if(type==0x100&&item_spell(&it)){
  it.enchant=pick_affix(&it,0,0,level,1,0,&state);
  unsigned multiplier=item_charge_multiplier(&it);it.charges*=multiplier;it.max_charges*=multiplier;
  return it;
 }
 unsigned pre=magic_roll(&state,4),post=magic_roll(&state,3);
 if(pre&&!post){if(magic_roll(&state,2))post=1;else pre=0;}
 unsigned low=level/2,alignment=0;if(low>25)low=25;
 if(!pre){unsigned id=pick_affix(&it,0,low,level,1,0,&state);it.enchant=id;if(id)alignment=item_prefixes[id-1].alignment;}
 if(post)it.enchant|=pick_affix(&it,1,low,level,1,alignment,&state)<<7;
 item_apply_durability(&it);return it;
}
Item __attribute__((section(".text.ui"))) game_quest_loot(unsigned q,uint32_t seed){
 unsigned monster=q==2?MONSTER_GHARBAD:q==3?MONSTER_ZHAR:q==11?MONSTER_WARLORD:MONSTER_BLACK_KNIGHT;
 unsigned level=q==4?28+15*game_difficulty:game_monster_level(monster),base=q==4?26:game_monster_base_level(monster);
 unsigned kind=unique_drop_candidate(seed^0x51u,level);
 if(q==2){
  /* Retail player-killed Gharbad: CreateTypeItem(onlygood, ITYPE_MACE). */
  unsigned count=0,pick=0;int type=item_base(ITEM_MACE)->type;
  for(unsigned pass=0;pass<2;pass++){
   if(pass){if(!count){kind=ITEM_MACE;break;}pick=item_seed(seed)%count;}
   for(unsigned k=1;k<ITEM_COUNT;k++)if(item_defs[k].weight&&item_base(k)->type==type&&item_base(k)->min_level<=8){
    if(!pass)count++;else if(!pick--){kind=k;goto chosen;}
   }
  }
  chosen:;
  Item it=item_create_level(kind,seed,8);item_generate_magic(&it,8,1);return it;
 }
 if(kind==ITEM_EMPTY)kind=ITEM_SHORT_SWORD;
 return unique_drop_item(kind,seed,base);
}
void game_item_monster_drop(Game *g,int index){
 const Enemy *e=&g->enemies[index];
 if(game_campaign_layout()&&e->kind==MONSTER_BUTCHER){if(!quest_progress.stage[6])quest_progress.stage[6]=129;game_reward_tick(g);return;}
 if(game_quest_npc_killed(g,e))return;
 unsigned unique=game_monster_is_unique(e->kind);
 if(!unique&&game_mushroom_killed(g,e))return;
 game_finale_killed(g,e);if(e->kind==MONSTER_DIABLO)return;
 if(e->kind==MONSTER_KING){game_king_killed(g,e);return;}
 /* Stable identity, independent of kill order / clock / owning player. */
 uint32_t seed=item_seed(campaign_seed^(dungeon_level*0x9e3779b9u)^(index*0x45d9f3bu)^(e->kind*0x85ebca6bu));
 if(!unique&&e->kind!=MONSTER_BUTCHER&&item_seed(seed)%100>40)return;
 /* Rnd[U]Item uses adjusted mLevel, while SetupAllItems receives raw
  * MData->mLevel for affixes, books and staff properties. */
 unsigned level=game_monster_level(e->kind),base_level=game_monster_base_level(e->kind);
 unsigned kind=unique?unique_drop_candidate(seed^0x51u,level):item_seed(seed^0x21u)%100>25?0:item_candidate(seed^0x51u,level);
 if(kind==ITEM_EMPTY)return;
 Item it;
 if(unique)it=unique_drop_item(kind,seed,base_level);
 else {it=item_create_level(kind,seed,base_level);item_generate_magic(&it,base_level,0);}
 if(e->kind==MONSTER_BUTCHER)for(unsigned k=1;k<ITEM_COUNT;k++){const BaseItemDef *base=item_base(k);if(base&&base->unique_base==item_uniques[0].base){it=item_create(k,seed);item_make_unique(&it,0);kind=k;break;}}
 int x=-1,y=-1;
 for(int r=0;r<=3&&x<0;r++)for(int dy=-r;dy<=r&&x<0;dy++)for(int dx=-r;dx<=r;dx++){
  if(dx!=-r&&dx!=r&&dy!=-r&&dy!=r)continue;
  int xx=e->x+dx,yy=e->y+dy;if(!game_item_space(g,xx,yy))continue;
  /* A nearby substitute must be connected, never across a wall corner. */
  int tx=e->x,ty=e->y,ok=1;
  while(tx!=xx||ty!=yy){int sx=(xx>tx)-(xx<tx),sy=(yy>ty)-(yy<ty);
   if(!game_walkable(g,tx+sx,ty+sy)||(sx&&sy&&(!game_walkable(g,tx+sx,ty)||!game_walkable(g,tx,ty+sy)))){ok=0;break;}tx+=sx;ty+=sy;
  }
  if(ok){x=xx;y=yy;break;}
 }
 if(x<0)return;
 for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){game_drop_put(g,item_to_drop(&it,x,y));game_item_sound(g,item_sound_kind(&it),0,x,y);return;}
}

/* Native RepairItem/RechargeItem: permanent maximum loss, owner-local RNG. */
int game_skill_item(Game *g,unsigned index){
 if(g->hp<=0)return INV_BUSY;
 if(game_hero_class()==1){
  if(g->area)return INV_TOWN;
  int target=-1,best=10;
  for(unsigned i=0;i<MAX_PROPS;i++)if(game_prop_trapped(g,i)){int dx=g->props[i].x-g->x,dy=g->props[i].y-g->y,d=dx*dx+dy*dy;if(d<best){target=i;best=d;}}
  if(target<0)return INV_REQUIREMENTS;
  unsigned roll=item_seed(campaign_seed^g->ticks^(++hero_progress.skill_nonce)*0x154u)%100;
  int chance=2*(int)game_effective_stat(2)-5*(int)game_dungeon_depth();
  if((int)roll<=chance){g->trap_spent|=(uint64_t)1<<target;return INV_OK;}return INV_REQUIREMENTS;
 }
 Inventory *bag=game_inventory();if(index>=ITEM_SLOTS||!item_valid(&bag->items[index]))return INV_REQUIREMENTS;
 Item *it=&bag->items[index];unsigned level=hero_progress.level?hero_progress.level:1;
 uint32_t seed=item_seed(campaign_seed^g->ticks^it->seed^(hero_progress.skill_nonce+1u)*0x37u);
 if(game_hero_class()==0){
  unsigned max=item_max_durability(it),now=item_durability(it),repair=0;
  if(!max||max==ITEM_INDESTRUCTIBLE||now==max)return INV_REQUIREMENTS;
  hero_progress.skill_nonce++;
  do{
   seed=item_seed(seed+37);repair+=level+seed%level;
   unsigned loss=max/(level+9);if(!loss)loss=1;max-=loss;if(it->flags&ITEM_PHYSICAL){if(item_is_staff(it))it->wear_loss=(it->wear_loss&0xff00u)|max;else it->wear_loss=max;}else it->wear_loss+=loss;
   if(!max){*it=(Item){.kind=ITEM_EMPTY};game_items_update(g);return INV_OK;}
  }while(now+repair<max);
  item_set_durability(it,now+repair>max?max:now+repair);
 }else{
  if(!item_is_staff(it)||!item_spell(it)||it->charges==it->max_charges)return INV_REQUIREMENTS;
  hero_progress.skill_nonce++;
  unsigned book=spell_defs[item_spell(it)].book,range=book?level/book:0;
  unsigned amount=(range?seed%range:0)+1; /* Native undefined/non-book divisors guarded. */
  unsigned charges=it->charges;
  do{if(!it->max_charges)break;it->max_charges--;if(!it->max_charges){charges=0;break;}charges+=amount;}while(charges<it->max_charges);
  it->charges=charges>it->max_charges?it->max_charges:charges;
 }
 game_items_update(g);return INV_OK;
}
