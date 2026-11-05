#include "item_effect_cache.h"
#include "performance_options.h"
static unsigned effect_cache_valid,effect_cache_next;
void item_effect_cache_reset(void){effect_cache_valid=effect_cache_next=0;}
/* Trust lives only inside one simulation step. The first select still checks
 * every equipped item; normal recalculation always validates the full key.
 * Bits8..11 say which owner keys were checked in the current step. */
void item_effect_cache_step(unsigned begin){effect_cache_valid=(effect_cache_valid&15u)|(begin?0x80000000u:0);}
static void effect_owner_checked(unsigned owner){
 if(owner<4&&(effect_cache_valid&0x80000000u))effect_cache_valid|=1u<<(8+owner);
}
#include "../generated/item_monster_class.h"
static int item_monster_is_demon(unsigned kind){return kind<MONSTER_TYPES&&monster71_demon[kind];}
ItemEffects item_effects;
static int clamp_stat(int value){return value<0?0:value;}
static unsigned resolve_equipment(const Inventory *bag,int stats[4]){
 const Item *slots[EQUIP_COUNT]={0};unsigned mask=0;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT||it->pos<BAG_CELLS||it->pos>=BAG_CELLS+EQUIP_COUNT||(item_defs[it->kind].durability&&!item_durability(it)))continue;
  unsigned slot=it->pos-BAG_CELLS;slots[slot]=it;mask|=1u<<slot;
  for(unsigned s=0;s<4;s++)stats[s]+=item_bonus(it,3+s);
 }
 /* Native CalcSelfItems: start with every equipped item, monotonically remove
  * unusable items and their bonuses. A removed item stays removed this pass. */
 unsigned changed;
 do{changed=0;for(unsigned slot=0;slot<EQUIP_COUNT;slot++)if(mask&(1u<<slot)){
  const Item *it=slots[slot];const ItemDef *d=&item_defs[it->kind];
  if(stats[0]<(int)item_strength(it)||stats[1]<(int)item_magic(it)||stats[2]<d->dexterity){
   mask&=~(1u<<slot);changed=1;for(unsigned s=0;s<4;s++)stats[s]-=item_bonus(it,3+s);
  }
 }}while(changed);
 return mask;
}
unsigned item_equipment_mask(const Inventory *bag,unsigned strength,unsigned magic,unsigned dexterity){int stats[4]={(int)strength,(int)magic,(int)dexterity,0};return resolve_equipment(bag,stats);}
static void item_build_effects_uncached(const HeroProgress *base,const Inventory *bag,unsigned cls,ItemEffects *out){
 *out=(ItemEffects){0};int stats[4]={base->strength,base->magic,base->dexterity,base->vitality};
 out->usable=resolve_equipment(bag,stats);int life=0,mana=0,light=10,resist[3]={0};out->weapon_low=out->weapon_high=1;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT||it->pos<BAG_CELLS||it->pos>=BAG_CELLS+EQUIP_COUNT||!(out->usable&(1u<<(it->pos-BAG_CELLS))))continue;
  if(!item_unidentified(it))for(unsigned p=0;p<item_power_count(it);p++){
   ItemPower a=item_power_at(it,p);
   if(a.power==52)out->special_flags|=ITEM_HALF_TRAP;
   if(a.power==74)out->special_flags|=ITEM_INFRA;
   if(a.power==45)out->special_flags|=ITEM_THORNS;
   if(a.power==46)out->special_flags|=ITEM_NOMANA;
   if(a.power==70)out->special_flags|=ITEM_ZERORES;
   if(a.power==73)out->combat_flags|=128;
   if(a.power==72)out->special_flags|=ITEM_DRAIN;
   if(a.power==69)out->special_flags|=ITEM_DEMON;
   if(a.power==62)out->special_flags|=ITEM_RANDOM_ARROW;
   if(a.power==57)out->armor_pierce+=item_power_value(it,p);
   unsigned e=(a.power==17||a.power==43)?2:0;
   uint16_t *range=(a.power==16||a.power==17)?out->melee_element:(a.power==42||a.power==43)?out->arrow_element:0;
   if(range){range[e]+=a.low;range[e+1]+=a.high;}
   /* Flamedart sets FIRE_ARROWS then FIREDAM in the same native fire range. */
   if((item_flags(it)&ITEM_BOW)&&a.power==16&&item_has_power(it,42)){out->arrow_element[0]+=a.low;out->arrow_element[1]+=a.high;}
  }
  out->spell_bonus+=item_bonus(it,20);light+=item_bonus(it,21);
  out->armor+=item_ac(it);out->damage_percent+=item_bonus(it,0);out->damage_flat+=item_bonus(it,1);out->to_hit+=item_bonus(it,9);out->damage_taken+=item_bonus(it,13);
  unsigned speed=item_bonus(it,16);if(speed>(out->combat_flags&7))out->combat_flags=(out->combat_flags&~7u)|speed;
  if(item_bonus(it,17))out->combat_flags|=8;
  unsigned recovery=item_bonus(it,18);if(recovery>((out->combat_flags>>4)&3))out->combat_flags=(out->combat_flags&~48u)|(recovery<<4);
  if(item_bonus(it,19))out->combat_flags|=64;
  unsigned steal=item_bonus(it,14);if(steal>out->mana_steal)out->mana_steal=steal;steal=item_bonus(it,15);if(steal>out->life_steal)out->life_steal=steal;
  life+=item_bonus(it,7);mana+=item_bonus(it,8);for(unsigned e=0;e<3;e++)resist[e]+=item_bonus(it,10+e);
  if(it->pos==BAG_CELLS+EQUIP_HAND){out->weapon_low=item_weapon_low(it);out->weapon_high=item_weapon_high(it);out->weapon_flags=item_flags(it);}
 }
 if(!(out->usable&(1u<<EQUIP_HAND))&&(out->usable&(1u<<EQUIP_SHIELD)))out->weapon_high=3;
 out->light_radius=light<2?2:light>15?15:light;
 int vit=stats[3]-base->vitality,mag=stats[1]-base->magic;
 /* Arithmetic half for negative rogue bonuses, matching retail's signed >>1. */
 life+=cls==0?2*vit:cls==1?vit+(vit>=0?vit/2:-((-vit+1)/2)):vit;
 mana+=cls==2?2*mag:cls==1?mag+(mag>=0?mag/2:-((-mag+1)/2)):mag;
 int hp=base->max_hp_q6/64+life,mp=base->max_mana_q6/64+mana;
 out->max_hp=hp>0?(hp<32768?hp:32767):1;out->max_mana=mp>0?(mp<32768?mp:32767):0;
 for(unsigned s=0;s<4;s++)out->stats[s]=clamp_stat(stats[s]);
 for(unsigned e=0;e<3;e++)out->resist[e]=(out->special_flags&ITEM_ZERORES)?0:resist[e]>75?75:clamp_stat(resist[e]);
}
/* Validate directly against a candidate key. Avoid building/zeroing a120-byte
 * temporary key and copying equipped items on every character switch. */
static int effect_base_matches(const ItemEffectKey *key,const HeroProgress *base,unsigned cls){
 return key->hp==base->max_hp_q6&&key->mana==base->max_mana_q6&&key->cls==cls&&key->stats[0]==base->strength&&key->stats[1]==base->magic&&key->stats[2]==base->dexterity&&key->stats[3]==base->vitality;
}
static int effect_matches(const ItemEffectKey *key,const HeroProgress *base,const Inventory *bag,unsigned cls){
 if(!effect_base_matches(key,base,cls))return 0;
 unsigned n=0;
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&bag->items[i];
  if(it->kind>=ITEM_COUNT||it->pos<BAG_CELLS||it->pos>=BAG_CELLS+EQUIP_COUNT)continue;
  if(n==EQUIP_COUNT)return 0;
  const uint32_t *a=(const uint32_t*)it,*b=(const uint32_t*)&key->equipped[n++];
  for(unsigned word=0;word<sizeof(Item)/4;word++)if(a[word]!=b[word])return 0;
 }
 /* Stored zero tail also distinguishes removing the last equipped item. */
 for(;n<EQUIP_COUNT;n++){const uint32_t *a=(const uint32_t*)&key->equipped[n];for(unsigned word=0;word<sizeof(Item)/4;word++)if(a[word])return 0;}
 return 1;
}
void item_build_effects(const HeroProgress *base,const Inventory *bag,unsigned cls,ItemEffects *out){
 if(!coop_active){item_effect_cache_reset();item_build_effects_uncached(base,bag,cls,out);return;}
 ItemEffectCache *cache=audio_item_cache();
 /* Each live hero owns one slot; copied previews retain associative lookup. */
 unsigned owner=4;for(unsigned i=0;i<4;i++)if(bag==&coop_players[i].hero.inventory){owner=i;break;}
 if(owner<4){
  effect_cache_valid&=~(1u<<(8+owner));
  if((effect_cache_valid&(1u<<owner))&&effect_matches(&cache[owner].key,base,bag,cls)){*out=cache[owner].value;effect_owner_checked(owner);return;}
 }else for(unsigned i=0;i<4;i++)if((effect_cache_valid&(1u<<i))&&effect_matches(&cache[i].key,base,bag,cls)){*out=cache[i].value;return;}
 ItemEffectKey key={0};key.hp=base->max_hp_q6;key.mana=base->max_mana_q6;key.cls=cls;
 key.stats[0]=base->strength;key.stats[1]=base->magic;key.stats[2]=base->dexterity;key.stats[3]=base->vitality;
 unsigned n=0;for(unsigned i=0;i<ITEM_SLOTS;i++){const Item *it=&bag->items[i];if(it->kind<ITEM_COUNT&&it->pos>=BAG_CELLS&&it->pos<BAG_CELLS+EQUIP_COUNT){if(n==EQUIP_COUNT){item_build_effects_uncached(base,bag,cls,out);return;}key.equipped[n++]=*it;}}
 item_build_effects_uncached(base,bag,cls,out);unsigned at=owner<4?owner:effect_cache_next++&3;
 cache[at].key=key;cache[at].value=*out;effect_cache_valid=(effect_cache_valid&~(1u<<(8+at)))|(1u<<at);effect_owner_checked(owner);
}
static void item_restore_effects(void){
#if PERF_EFFECT_SELECT
 unsigned owner=coop_turn;
 if(coop_active&&owner<4&&(effect_cache_valid&(1u<<(8+owner)))){
  ItemEffectCache *c=&audio_item_cache()[owner];
  if(effect_base_matches(&c->key,&hero_progress,game_hero_class())){item_effects=c->value;return;}
 }
#endif
 item_build_effects(&hero_progress,game_inventory(),game_hero_class(),&item_effects);
}
unsigned game_light_radius(void){return item_effects.light_radius;}
unsigned game_vision_radius(void){unsigned r=game_light_radius();return r<10?10:r;}
int game_spell_bonus(void){return item_effects.spell_bonus;}
unsigned game_effective_stat(unsigned stat){return stat<4?(unsigned)item_effects.stats[stat]:0;}
unsigned game_character_max_hp(const CharacterState *h){ItemEffects effects;item_build_effects(&h->progress,&h->inventory,h->reserved&255,&effects);return effects.max_hp;}
void game_items_update(Game *g){
 int old_hp=item_effects.max_hp,old_mana=item_effects.max_mana;game_items_recalc();
 if(g->hp>0){g->hp+=item_effects.max_hp-old_hp;if(g->hp<0)g->hp=0;if(g->hp>item_effects.max_hp)g->hp=item_effects.max_hp;}
 int mana=(int)g->mana-hero_progress.mana_debt+item_effects.max_mana-old_mana;
 hero_progress.mana_debt=mana<0?-mana:0;g->mana=mana>0?mana:0;if(g->mana>item_effects.max_mana)g->mana=item_effects.max_mana;
 if(item_effects.special_flags&ITEM_NOMANA){g->mana=0;hero_progress.mana_fraction=0;}
 g->max_mana=item_effects.max_mana;if(!hero_progress.mana_debt&&g->mana==g->max_mana)hero_progress.mana_fraction=0;
}
int game_melee_hit(const Game *g,unsigned enemy){
 if(enemy>=MAX_ENEMIES||!g->enemies[enemy].alive||!game_monster_damageable(g,enemy))return 0;
 int chance=(int)game_effective_stat(2)/2+hero_progress.level+50+(game_hero_class()==0?20:0)+item_effects.to_hit+item_effects.armor_pierce-(int)game_monster_armor(g->enemies[enemy].kind);
 if(chance<5)chance=5;if(chance>95)chance=95;
 unsigned roll=item_seed(g->ticks^((enemy+1)*0x91e10da5u)^((coop_active?coop_turn:0)*0x9e3779b9u))%100;
 return game_spell_stoned(enemy)||roll<(unsigned)chance;
}
unsigned game_player_damage(Game *g,unsigned damage,unsigned element){
 if(g->hp<=0||element>3)return 0;
 if(!element&&damage){int adjusted=(int)damage+item_effects.damage_taken;damage=adjusted>0?(unsigned)adjusted:1;}
 if(element){unsigned slot=element==1?2:element==2?0:1;damage=damage*(100-item_effects.resist[slot])/100;}
 return game_spell_absorb(g,damage);
}

void game_item_effects_step(Game *g){
 if(item_effects.special_flags&ITEM_NOMANA){g->mana=0;hero_progress.mana_fraction=0;}
 /* Native drains4/64 HP per20Hz frame. Preserve fraction in former padding. */
 if(!g->area&&g->hp>0&&(item_effects.special_flags&ITEM_DRAIN)){
  if(++hero_progress.life_drain_fraction==16){hero_progress.life_drain_fraction=0;g->hp--;}
 }
}
void game_restore_mana(Game *g,unsigned amount,int full){
 if(item_effects.special_flags&ITEM_NOMANA){g->mana=0;hero_progress.mana_fraction=0;return;}
 int mana=full?g->max_mana:(int)g->mana-hero_progress.mana_debt+(int)amount;
 hero_progress.mana_debt=mana<0?-mana:0;g->mana=mana>0?mana:0;
 if(g->mana>=g->max_mana&&!hero_progress.mana_debt){g->mana=g->max_mana;hero_progress.mana_fraction=0;}
}

/* Retail PlrHitMonst leech is melee-only; spells and arrows use other paths.
 * GBA life is whole HP; mana keeps its existing fractional remainder. */
unsigned game_weapon_element_roll(uint32_t seed,int bow,unsigned element){
 if(element>1)return 0;const uint16_t *r=(bow?item_effects.arrow_element:item_effects.melee_element)+element*2;
 return r[0]+item_seed(seed^(element?0x9e3779b9u:0x91e10da5u))%(r[1]-r[0]+1);
}
/* Resolve components before one hit/kill callback; leech remains physical-only. */
void game_weapon_hit(Game *g,unsigned enemy,unsigned physical,unsigned fire,unsigned lightning){
 if(enemy>=MAX_ENEMIES||!g->enemies[enemy].alive)return;
 unsigned kind=g->enemies[enemy].kind,immune=game_monster_immunities(kind),resist=game_monster_resistances(kind);
 fire=(immune&2)?0:(resist&2)?fire/4:fire;lightning=(immune&4)?0:(resist&4)?lightning/4:lightning;
 game_damage_enemy(g,enemy,physical+fire+lightning);
}
/* Retail weapon family applies even to a red, unusable equipped weapon. */
unsigned game_weapon_type(void){
 const Inventory *bag=game_inventory();
 for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind<ITEM_COUNT&&bag->items[i].pos==BAG_CELLS+EQUIP_HAND){const BaseItemDef *b=item_base(bag->items[i].kind);return b?(unsigned)b->type:0;}
 return 0;
}
unsigned game_melee_physical(unsigned kind,unsigned damage,uint32_t seed){
 if(!game_hero_class()&&item_seed(seed^0x43524954u)%100<hero_progress.level)damage*=2;
 if(item_monster_is_demon(kind)){if(item_effects.special_flags&ITEM_DEMON)damage*=3;}
 else if(kind<MONSTER_TYPES){unsigned type=game_weapon_type();if(type==1||type==4){
  if((type==1)==!!monster_undead[kind])damage-=damage>>1;else damage+=damage>>1;
 }}return damage;
}
void game_melee_damage(Game *g,unsigned enemy,unsigned damage){
 if(g->hp<=0||enemy>=MAX_ENEMIES||!g->enemies[enemy].alive||!damage||!game_monster_damageable(g,enemy))return;
 uint32_t seed=g->ticks^((enemy+1)*0x632be5abu)^((coop_active?coop_turn:0)*0x85157af5u);
 damage=game_melee_physical(g->enemies[enemy].kind,damage,seed);
 unsigned life_q6=damage*item_effects.life_steal*64/100;
 if(item_effects.combat_flags&128)life_q6+=item_seed(seed^0x554e4443u)%(damage*8);
 life_q6+=hero_progress.life_steal_fraction;hero_progress.life_steal_fraction=life_q6%64;
 unsigned life=life_q6/64,mp=damage*item_effects.mana_steal*64/100;
 /* Kill experience can refill resources, so apply leech before kill callbacks. */
 g->hp+=life;if(g->hp>=game_max_hp()){g->hp=game_max_hp();hero_progress.life_steal_fraction=0;}
 mp+=hero_progress.mana_fraction;hero_progress.mana_fraction=mp%64;game_restore_mana(g,mp/64,0);
 game_weapon_hit(g,enemy,damage,game_weapon_element_roll(seed,0,0),game_weapon_element_roll(seed,0,1));
 if(item_effects.combat_flags&8)game_monster_knockback(g,enemy,g->x,g->y);
 game_wear_equipment(g,WEAR_MELEE);
}

/* Full animation and AFNum are distinct. Bits5..7 pin the weapon animation,
 * bits0..4 the total ticks; the existing co-op field needs no extra RAM.
 * Categories: unarmed, sword/mace, axe, staff, bow, shield only. */
static const uint8_t attack_frames[3][6][2]={
 {{16,9},{16,9},{20,10},{16,11},{16,11},{16,9}},
 {{18,10},{18,10},{22,13},{16,11},{12,7},{18,10}},
 {{20,12},{16,12},{24,16},{16,12},{20,16},{16,9}}
};
unsigned game_attack_duration(int bow){
 unsigned type=0,usable=item_effects.usable,speed=item_effects.combat_flags&7;
 if(usable&(1u<<EQUIP_HAND)){unsigned weapon=game_weapon_type();type=bow?4:weapon==2?2:weapon==10?3:1;}
 else if(usable&(1u<<EQUIP_SHIELD))type=5;
 if(type==4&&speed>2)speed=0;
 /* Each attack starts with the same native idle-entry frame convention:
  * frame1 has already advanced, so readiness's skip is lost; swiftness skips
  * one, speed/haste two. Never give the first swing an extra speed bonus.
  * Repeat cadence now includes recovery, not just the pre-impact frames. */
 if(speed)speed=speed==4?2:speed-1;
 return (type<<5)|(attack_frames[game_hero_class()][type][0]-speed);
}
unsigned game_attack_hit_wait(unsigned cls,unsigned timing){
 unsigned type=timing>>5;if(cls>2)cls=0;if(type>5)type=0;
 return attack_frames[cls][type][0]-attack_frames[cls][type][1]+1;
}

/* Jarulf's20Hz block/recovery durations; presentation uses the pinned clock.
 * Highest recovery tier wins; timers are pinned per player on entry. */
unsigned game_recovery_duration(unsigned cls,int block){
 static const uint8_t hit[3]={6,7,8},shield[3]={2,4,6};if(cls>2)cls=0;
 return block?((item_effects.combat_flags&64)?2:shield[cls]):hit[cls]-((item_effects.combat_flags>>4)&3);
}
unsigned game_block_chance(const Game *g,unsigned enemy_level){
 if(g->hp<=0||g->area||g->move_wait||g->hit_flash||SPELL_CASTING(g->swing_target)||(item_effects.weapon_flags&ITEM_BOW)||!(item_effects.usable&(1u<<EQUIP_SHIELD)))return 0;
 int chance=(int)game_effective_stat(2)+30-10*(int)game_hero_class()+2*((int)hero_progress.level-(int)enemy_level);
 return chance<0?0:chance>100?100:(unsigned)chance;
}
static void receive_physical(Game *g,unsigned enemy,unsigned kind,int sx,int sy,unsigned damage,unsigned block_roll,int melee){
 if(g->hp<=0||kind>=MONSTER_TYPES||!damage)return;
 unsigned blocked=block_roll<game_block_chance(g,game_monster_level(kind));
 int impact=(int)damage+item_effects.damage_taken;if(impact<1)impact=1;
 if(melee&&!blocked&&monster71_native[kind]==3&&hero_progress.max_hp_q6>64){hero_progress.max_hp_q6-=64;game_items_recalc();if(g->hp>(int)game_max_hp())g->hp=game_max_hp();}
 unsigned taken=blocked?0:game_player_damage(g,damage,0);
 g->hp-=taken;if(g->hp<0)g->hp=0;
 if(melee&&!blocked&&(item_effects.special_flags&ITEM_THORNS)){
  unsigned reflected=1+item_seed(g->ticks^((enemy+1)*0x4a71u)^((coop_active?coop_turn:0)*0x85157af5u))%3;
  game_damage_enemy(g,enemy,reflected);
 }
 /* Native StartPlrHit does not stagger below the hero level. */
 if(!g->hp||(!blocked&&impact<hero_progress.level))return;
 if(blocked){int dx=sx-g->x,dy=sy-g->y;
  static const int8_t vx[8]={1,0,-1,-1,-1,0,1,1},vy[8]={1,1,1,0,-1,-1,-1,0};
  dx=(dx>0)-(dx<0);dy=(dy>0)-(dy<0);for(unsigned d=0;d<8;d++)if(vx[d]==dx&&vy[d]==dy){g->facing=d;break;}
 }
 g->reaction_block=blocked;g->reaction_duration=game_recovery_duration(game_hero_class(),blocked);g->hit_flash=g->reaction_duration;
 g->attack_wait=0;g->attack_duration=0;if(g->attack_queued!=2)g->attack_queued=0;g->swing_target=-1;
}

int game_infravision(void){return hero_progress.infra_ticks||(item_effects.special_flags&ITEM_INFRA);}

void game_receive_melee(Game *g,unsigned enemy,unsigned damage,unsigned block_roll){
 if(enemy>=MAX_ENEMIES||!g->enemies[enemy].alive)return;const Enemy *e=&g->enemies[enemy];receive_physical(g,enemy,e->kind,e->x,e->y,damage,block_roll,1);
}
void game_receive_arrow(Game *g,unsigned kind,int sx,int sy,unsigned damage,unsigned block_roll){receive_physical(g,0,kind,sx,sy,damage,block_roll,0);}
