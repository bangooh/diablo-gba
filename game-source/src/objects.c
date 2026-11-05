#include "game.h"
#include "../generated/quest_objects71.h"
#include "../generated/environment76.h"
#include "../generated/story_names76.h"
#include "coop.h"
#include "hazards.h"
#include "wall_traps.h"
#include "../generated/explosive_barrels.h"
#include "../generated/door_frames.h"
#include "../generated/late_quests.h"
#include "../generated/finale_assets.h"
#include "../generated/water_candles.h"
#include "../generated/quest_rock.h"
#include "../generated/quest_valor.h"
#include "../generated/quest_remaining.h"
#include "../generated/quest_banner.h"
#include "../generated/prop_frames.h"
#include "../generated/cathedral_props.h"
#include "../generated/butcher_assets.h"
#include "../generated/environment_props.h"
#include "../generated/fountain_props.h"
/* Source-guided containers, linked chest missiles and explosive barrels.
 * Immutable map/position identity substitutes for _oRndSeed without growing
 * the SRAM payload. Time, open order, actors and prop.state never enter it. */
int game_prop_target(const Game *g){
 int t=g->attack_wait?g->swing_target:g->target,i=-t-2;
 return !g->area&&t<=-2&&i<MAX_PROPS&&g->props[i].kind&&!game_prop_is_wall_trap(g->props[i].kind)&&!game_prop_is_theme_decor(g->props[i].kind)&&g->props[i].kind!=PROP_BRAZIER&&g->props[i].kind!=PROP_WATER_CANDLE&&g->props[i].kind!=PROP_VILE_CIRCLE&&g->props[i].kind!=PROP_VILE_ALTAR&&(!g->props[i].state||game_prop_repeatable(g->props[i].kind))?i:-1;
}
static uint32_t random_n(uint32_t *s,unsigned n){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s%n;}
#include "environment_objects.inc"
static uint32_t world_seed(const Game *g){
 uint32_t h=2166136261u;
 for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++)h=(h^g->map[y][x])*16777619u;
 return h;
}
#include "wall_traps.inc"
/* Quest bodies occupy family1 even after death/reload. Containers must use
 * the compatible family5 palette instead of introducing a new translation. */
static __attribute__((section(".text.ui"),noinline)) unsigned container_monster(const Game *g,unsigned family){
 if(family==1)for(unsigned i=0;i<MAX_ENEMIES;i++){
  const Enemy *e=&g->enemies[i];
  if((e->alive||e->x||e->y)&&(e->kind==MONSTER_GHARBAD||e->kind==MONSTER_WARLORD)){family=5;break;}
 }
 return game_monster_variant(g,family);
}
static void loot(const Game *g,int i,uint32_t seed,PropLoot *out){
 const Prop *p=&g->props[i];*out=(PropLoot){0,{0,0,0},-1};
 seed^=(uint32_t)(p->x+p->y*80+i*6400+p->kind*409600)*0x9e3779b9u;if(!seed)seed=1;
 unsigned useful=0;
 if(p->kind==PROP_SARC||p->kind==PROP_BARREL){
  unsigned roll=random_n(&seed,10);
  if(roll>=8){out->monster=container_monster(g,(dungeon_level==2&&butcher_quest)?1:random_n(&seed,2)?5:1);return;}
  if(roll>=(p->kind==PROP_SARC?3u:2u))return;
  out->count=1;useful=p->kind==PROP_BARREL&&random_n(&seed,3)==0;
 }else if(p->kind==PROP_CHEST||p->kind==PROP_CHEST_MEDIUM||p->kind==PROP_CHEST_LARGE){
  if(game_shrine_chest_eligible(g,(unsigned)i))seed^=game_shrine_loot_salt(g);if(!seed)seed=1;
  unsigned size=p->kind==PROP_CHEST?1:p->kind==PROP_CHEST_MEDIUM?2:3;
  out->count=random_n(&seed,size+1);useful=random_n(&seed,8)==0;
 }
 for(unsigned n=0;n<out->count;n++){
  out->items[n]=useful?1+random_n(&seed,2):random_n(&seed,100)<=25?item_candidate(seed,2*game_dungeon_depth()):0;
 }
}
void game_prop_loot(const Game *g,int i,PropLoot *out){
 if(i<0||i>=MAX_PROPS){*out=(PropLoot){0,{0,0,0},-1};return;}
 loot(g,i,world_seed(g),out);
}
unsigned game_prop_reservations(const Game *g){
 unsigned count=0;uint32_t seed=world_seed(g);
 for(int i=0;i<MAX_PROPS;i++)if(g->props[i].kind&&!g->props[i].state){PropLoot r;loot(g,i,seed,&r);count+=r.monster>=0;}
 return count;
}
static int monster_space(const Game *g,int x,int y){
 if(!game_walkable(g,x,y)||(g->x==x&&g->y==y)||game_monster_blocks(g,x,y,-1))return 0;
 for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive&&g->enemies[i].x==x&&g->enemies[i].y==y)return 0;
 return 1;
}
/* Native BreakBarrel visits its complete 3x3 without a line-of-sight test.
 * An eight-byte pending set bounds chains to MAX_PROPS and avoids recursion.
 * Opening marks a barrel before its blast, so overlapping chains cannot replay
 * an explosion. Each distinct barrel can still hurt a shared nearby actor. */
static int explode_barrels(Game *g,unsigned first){
 uint8_t pending[MAX_PROPS/8]={0};pending[first>>3]|=(uint8_t)(1u<<(first&7));
 for(;;){
  unsigned i=0;while(i<MAX_PROPS&&!(pending[i>>3]&(1u<<(i&7))))i++;if(i==MAX_PROPS)break;
  pending[i>>3]&=(uint8_t)~(1u<<(i&7));Prop *p=&g->props[i];
  if(p->kind!=PROP_EXPLOSIVE_BARREL||p->state)continue;
  p->state=2;game_hazard_sound(1);uint32_t rng=item_seed(campaign_seed^dungeon_level*0x9e3779b9u^(i+1)*0x42524c58u^g->ticks);
  for(int y=(int)p->y-1;y<=(int)p->y+1;y++)for(int x=(int)p->x-1;x<=(int)p->x+1;x++){
   if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H)continue;
   game_trap_hit_cell(g,x,y,1,4,8,16,0,GAME_TRAP_FIREBOLT,&rng);
   for(unsigned n=0;n<MAX_PROPS;n++)if(g->props[n].kind==PROP_EXPLOSIVE_BARREL&&!g->props[n].state&&g->props[n].x==x&&g->props[n].y==y)pending[n>>3]|=(uint8_t)(1u<<(n&7));
  }
 }
 return 1;
}
int game_open_prop(Game *g,int i){
 if(i<0||i>=MAX_PROPS||!g->props[i].kind||(g->props[i].state&&!game_prop_repeatable(g->props[i].kind)))return 0;
 if(game_prop_is_wall_trap(g->props[i].kind))return 0;
 if(g->props[i].kind>=PROP_BRAZIER)return game_environment_use(g,i);
 if(g->props[i].kind==PROP_EXPLOSIVE_BARREL)return explode_barrels(g,(unsigned)i);
 if(game_prop_is_door(g->props[i].kind)){g->props[i].state=1;return 1;}
 if(g->props[i].kind>=PROP_BONE_ENTRY)return game_late_use(g,i);
 if(g->props[i].kind==PROP_MUSHROOM_PATCH)return game_mushroom_use(g,i);
 if(g->props[i].kind>=PROP_VILE_BOOK)return game_finale_use(g,i);
 if(g->props[i].kind==PROP_WATER_ENTRY)return dungeon_level==2&&quest_progress.state[13]?game_region_floor(g,REGION_WATER):0;
 if(g->props[i].kind>=PROP_KING_ENTRY&&g->props[i].kind<=PROP_KING_CRUX3)return game_king_use(g,(unsigned)i);
 if(g->props[i].kind==PROP_SNOTSPIL||g->props[i].kind==PROP_SIGN_CHEST)return game_banner_use(g,(unsigned)i);
 if(g->props[i].kind==PROP_BLIND_BOOK||g->props[i].kind==PROP_LACHDANAN)return game_quest_extra_use(g,(unsigned)i);
 if(g->props[i].kind==PROP_VALOR_BOOK||g->props[i].kind==PROP_VALOR_PEDESTAL)return game_quest_valor_use(g,(unsigned)i);
 if(g->props[i].kind>=PROP_DECOR_FIRST)return 0;
 unsigned trapped=game_prop_trapped(g,i)&&!game_wall_trap_linked(g,(unsigned)i);
 Prop *p=&g->props[i];PropLoot result;game_prop_loot(g,i,&result);
 p->state=1; /* Temporarily allow the opened tile during placement checks. */
 if(result.monster>=0){
  int slot=-1,x=p->x,y=p->y;
  for(int j=0;j<MAX_ENEMIES;j++)if(!g->enemies[j].alive){slot=j;break;}
  if(slot<0){p->state=0;return 0;}
  if(!monster_space(g,x,y)){
   int found=0;
   for(int dy=-1;dy<=1&&!found;dy++)for(int dx=-1;dx<=1&&!found;dx++)if(monster_space(g,p->x+dx,p->y+dy)){x=p->x+dx;y=p->y+dy;found=1;}
   if(!found){p->state=0;return 0;}
  }
  game_pack_forget((unsigned)slot);game_monster_reset(slot);g->enemy_attack[slot]=0;g->enemy_facing[slot]=0;
  g->enemies[slot]=(Enemy){x,y,game_monster_hp(result.monster,(i+1)*2654435761u),1,(uint8_t)result.monster,0};game_activate_nearby(g);
 }else{
  int xs[3],ys[3];unsigned n=0;
  for(int j=0;j<MAX_DROPS&&n<result.count;j++)if(!g->drops[j].active)n++;
  if(n<result.count){p->state=0;g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;}
  n=0;
  for(int radius=0;radius<=3&&n<result.count;radius++)for(int dy=-radius;dy<=radius&&n<result.count;dy++)for(int dx=-radius;dx<=radius&&n<result.count;dx++){
   if(dx!=-radius&&dx!=radius&&dy!=-radius&&dy!=radius)continue;
   int x=p->x+dx,y=p->y+dy;if(!game_item_space(g,x,y))continue;
   xs[n]=x;ys[n]=y;n++;
  }
  if(n<result.count){p->state=0;g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;}
  for(n=0;n<result.count;n++){
   uint32_t seed=item_seed(campaign_seed^((i+1)*0x9e3779b9u)^(n*0x45d9f3bu)^dungeon_level);
   Item it=item_create(result.items[n],seed);item_generate_magic(&it,2*game_dungeon_depth(),0);
   game_drop_put(g,item_to_drop(&it,xs[n],ys[n]));
   game_item_sound(g,item_sound_kind(&it),0,xs[n],ys[n]);
  }
 }
 if(trapped){
  /* _oVar4 belongs to the chest, not its opening tick or operating player. */
  uint32_t seed=item_seed(campaign_seed^dungeon_level*0x9e3779b9u^(i+1)*0x43545250u);
  unsigned type=seed%(game_dungeon_depth()<=8?2:3);
  g->trap_spent|=(uint64_t)1<<i;game_trap_fire(g,p->x,p->y,g->x,g->y,type,seed);
 }
 if(p->kind==PROP_SARC||p->kind==PROP_BARREL)p->state=2;
 return 1;
}

/* Automatic AI operations only open. Player and Telekinesis operations toggle.
 * Retail closing rejects bodies, items and remains; include port movement
 * reservations and all party members so a closing door cannot trap an actor. */
static int door_hero_at(int x,int y,int hx,int hy,unsigned moving,unsigned facing){
 static const int8_t dx[8]={1,0,-1,-1,-1,0,1,1},dy[8]={1,1,1,0,-1,-1,-1,0};
 return (x==hx&&y==hy)||(moving&&x==hx-dx[facing&7]&&y==hy-dy[facing&7]);
}
int game_operate_prop(Game *g,int i){
 if(i<0||i>=MAX_PROPS)return 0;
 Prop *p=&g->props[i];
 if(!game_prop_is_door(p->kind)||!p->state)return game_open_prop(g,i);
 int x=p->x,y=p->y;
 if(door_hero_at(x,y,g->x,g->y,g->move_wait,g->facing)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0))return 0;
 if(coop_active)for(unsigned n=0;n<4;n++)if((coop_members&(1u<<n))&&coop_players[n].hero.hp>0){
  const CoopPlayer *h=&coop_players[n];if(door_hero_at(x,y,h->x,h->y,h->move_wait,h->facing))return 0;
 }
 for(unsigned n=0;n<MAX_ENEMIES;n++){
  if(g->enemies[n].alive&&g->enemies[n].x==x&&g->enemies[n].y==y)return 0;
  if(game_corpses[n].ticks&&game_corpses[n].x==x&&game_corpses[n].y==y)return 0;
 }
 for(unsigned n=0;n<MAX_DROPS;n++)if(g->drops[n].active&&g->drops[n].x==x&&g->drops[n].y==y)return 0;
 p->state=0;return 1;
}
/* State0 closed,1 finished;2..13 encode elapsed animation ticks.
 * Keep the byte-sized SRAM layout and make mid-animation saves resumable. */
void game_props_step(Game *g){
 if(g->area)return;
 game_wall_traps_step(g);
 for(int i=0;i<MAX_PROPS;i++){
  Prop *p=&g->props[i];if(p->kind==PROP_FLAME_LEVER)flame_step(g,(unsigned)i);
  if(game_prop_is_flame(p->kind))continue;
  if(p->kind==PROP_BURNING_CROSS)environment_cross_tick(g,p);
  if(game_prop_is_wall_trap(p->kind)||p->state<2)continue;
  unsigned duration=p->kind==PROP_LACHDANAN?PROP_LACHDANAN_DEATH_FRAMES*2:game_prop_is_shrine(p->kind)?12:p->kind==PROP_SARC?(PROP_SARC_FRAMES-1)*3:p->kind==PROP_EXPLOSIVE_BARREL?PROP_BARRELEX_FRAMES-1:PROP_BARREL_FRAMES-1;
  if(++p->state>=duration+2)p->state=1;
 }
}
unsigned game_prop_frame(const Game *g,int i){
 const Prop *p=&g->props[i];
 if(p->kind==PROP_SLAIN_HERO)return PROP_THEME_DECAP_BASE+1;
 if(p->kind==PROP_STORY_BOOK)return PROP_STORY_BASE+4-2*(game_story_index()/3)+(p->state?1:0);
 if(p->kind==PROP_FLAME_LEVER)return PROP_FLAME_LEVER_BASE+(p->state?1:0);
 if(game_prop_is_flame(p->kind)){unsigned s=p->state;return PROP_FLAME_BASE+(s==0?0:s==1?10+g->ticks%10:s<=10?s-1:(15-s)*2);}
 if(game_prop_is_fountain(p->kind)){
  unsigned phase=(g->ticks/4+(p->x+p->y)%5)%5;
  if(p->kind==PROP_BLOOD_FOUNTAIN)return PROP_BLOOD_FOUNTAIN_BASE+phase;
  if(p->kind==PROP_PURIFYING_FOUNTAIN)return PROP_PURIFYING_FOUNTAIN_BASE+phase;
  if(p->kind==PROP_MURKY_FOUNTAIN)return PROP_MURKY_FOUNTAIN_BASE+phase;
  return PROP_TEAR_FOUNTAIN_BASE+(g->ticks/2+(p->x+p->y)%4)%4;
 }
 if(p->kind==PROP_THEME_CANDLE)return PROP_WATER_CANDLE_BASE+(g->ticks/4+(p->x+p->y)%4)%4;
 if(p->kind==PROP_BOOKSTAND)return PROP_FINALE_BOOK2+(p->state?2:0);
 if(p->kind==PROP_SKEL_BOOK)return PROP_FINALE_BOOK2+3+(p->state?2:0);
 if(p->kind==PROP_SKEL_FIRE)return PROP_SKEL_FIRE_BASE+(g->ticks%22)/4;
 if(p->kind>=PROP_THEME_BANNER_L&&p->kind<=PROP_THEME_BANNER_R){static const uint8_t frames[3]={1,0,2};return PROP_THEME_BANNER_BASE+frames[p->kind-PROP_THEME_BANNER_L];}
 if(p->kind==PROP_THEME_DECAP)return PROP_THEME_DECAP_BASE+(environment_seed(g,(unsigned)i)>>1)%8;
 if(p->kind==PROP_BURNING_CROSS)return PROP_BURNING_CROSS_BASE+(g->ticks/4+(p->x+p->y)%5)%5;
 if(p->kind==PROP_ARMOR_STAND)return PROP_LATE_ARMSTAND+(p->state?1:0);
 if(p->kind==PROP_WEAPON_RACK)return PROP_LATE_WEAPSTND+(p->state?1:0);
 if(p->kind==PROP_GOAT_SHRINE)return PROP_GOAT_SHRINE_BASE+(g->ticks/4+(p->x+p->y)%5)%5;
 if(p->kind==PROP_CAULDRON)return PROP_CAULDRON_BASE+(p->state?1:0);
 if(p->kind==PROP_BRAZIER)return PROP_BRAZIER_BASE+(g->ticks%PROP_BRAZIER_NATIVE_TICKS)/4;
 if(game_prop_is_shrine(p->kind))return (p->kind==PROP_SHRINE_L?PROP_SHRINE_L_BASE:PROP_SHRINE_R_BASE)+(p->state==0?0:p->state==1?3:(p->state-2)/4);
 /* Retail shelves start at frames3/4 and subtract2 when searched. */
 if(game_prop_is_bookcase(p->kind))return PROP_BOOKCASE_BASE+p->kind-PROP_BOOKCASE_L+(p->state?0:2);
 if(game_prop_is_wall_trap(p->kind))return game_wall_trap_frame(p->kind);
 if(p->kind==PROP_EXPLOSIVE_BARREL)return PROP_BARRELEX_BASE+(p->state==0?0:p->state==1?PROP_BARRELEX_FRAMES-1:p->state-2);
 if(p->kind==PROP_L2_DOOR_L||p->kind==PROP_L2_DOOR_R)return PROP_L2DOORS_BASE+p->kind-PROP_L2_DOOR_L+(p->state?2:0);
 if(p->kind==PROP_L3_DOOR_L||p->kind==PROP_L3_DOOR_R)return PROP_L3DOORS_BASE+p->kind-PROP_L3_DOOR_L+(p->state?2:0);
 if(p->kind==PROP_BONE_ENTRY)return PROP_FINALE_MCIRL;
 if(p->kind==PROP_WAR_ARMOR)return PROP_LATE_ARMSTAND+(p->state?1:0);
 if(p->kind==PROP_WAR_WEAPON)return PROP_LATE_WEAPSTND+(p->state?1:0);
 if(p->kind==PROP_BONE_BOOK||p->kind==PROP_WARLORD_BOOK||p->kind==PROP_BONE_TOME)return PROP_FINALE_BOOK2+(p->state?1:0);
 if(p->kind==PROP_BONE_LEVER)return PROP_FINALE_SWITCH4+(p->state?1:0);
 if(p->kind==PROP_MUSHROOM_PATCH)return PROP_FINALE_MUSHPTCH+(p->state?1:0);
 if(p->kind==PROP_VILE_BOOK)return PROP_FINALE_BOOK2+(p->state?1:0);
 if(p->kind==PROP_VILE_CIRCLE||p->kind==PROP_VILE_ENTRY)return PROP_FINALE_MCIRL+((p->kind==PROP_VILE_ENTRY||(p->x==g->x&&p->y==g->y))?1:0);
 if(p->kind==PROP_VILE_ALTAR)return PROP_FINALE_ALTBOY;
 if(p->kind==PROP_LAZ_STAND)return PROP_FINALE_LZSTAND+(p->state?1:0);
 if(p->kind==PROP_DIABLO_LEVER)return PROP_FINALE_SWITCH4+(p->state?1:0);
 /* All decorative changes share the same5fps clock. A fixed image offset
  * varies nearby candles without forcing background work on extra ticks. */
 if(p->kind==PROP_WATER_CANDLE){unsigned seed=item_seed(campaign_seed^(p->x+p->y*80));return PROP_WATER_CANDLE_BASE+((g->ticks/4+seed%4)%4);}
 if(p->kind==PROP_KING_ENTRY||p->kind==PROP_WATER_ENTRY)return 61;
 if(p->kind>=PROP_KING_LEVER&&p->kind<=PROP_KING_CRUX3)return 62+2*(p->kind-PROP_KING_LEVER)+(p->state?1:0);
 if(p->kind==PROP_SNOTSPIL)return PROP_SNOTSPIL_FRAME+(p->state?1:0);
 if(p->kind==PROP_SIGN_CHEST)return PROP_CHEST3_BASE+(p->state?2:0);
 if(p->kind==PROP_BLIND_BOOK)return PROP_BLIND_BOOK_FRAME+(p->state?1:0);
 if(p->kind==PROP_LACHDANAN)return !p->state?PROP_LACHDANAN_FRAME:PROP_LACHDANAN_DEATH_BASE+(p->state==1?PROP_LACHDANAN_DEATH_FRAMES-1:(p->state-2)/2);
 if(p->kind==PROP_VALOR_BOOK)return PROP_VALOR_BOOK_FRAME+(p->state?1:0);
 if(p->kind==PROP_VALOR_PEDESTAL)return PROP_VALOR_PEDESTAL_FRAME+(quest_progress.stage[9]?quest_progress.stage[9]-1:0);
 if(p->kind==PROP_ROCK_STAND)return PROP_ROCK_STAND_FRAME;
 if(p->kind==PROP_DOOR_L||p->kind==PROP_DOOR_R)return PROP_L1DOORS_BASE+(p->kind-PROP_DOOR_L)+(p->state?2:0);
 if(p->kind>=17)return PROP_TNUDEW_BASE+p->kind-17;
 if(p->kind>=13)return PROP_TNUDEM_BASE+p->kind-13;
 if(p->kind>=8)return PROP_TSOUL_BASE+p->kind-8;
 if(p->kind==PROP_SARC)return PROP_SARC_BASE+(p->state==0?0:p->state==1?PROP_SARC_FRAMES-1:(p->state-2)/3);
 if(p->kind==PROP_BARREL)return PROP_BARREL_BASE+(p->state==0?0:p->state==1?PROP_BARREL_FRAMES-1:p->state-2);
 /* OperateChest advances +2; frames1..3 and4..6 are separate orientations. */
 unsigned base=p->kind==PROP_CHEST_MEDIUM?PROP_CHEST2_BASE:p->kind==PROP_CHEST_LARGE?PROP_CHEST3_BASE:PROP_CHEST1_BASE;
 unsigned orientation=((p->x*73856093u)^(p->y*19349663u)^campaign_seed)&1;
 return base+orientation*3+(p->state?2:0);
}
/* Retail AddChestTraps: ten percent after the Cathedral; immutable seed.
 * trap_spent is shared floor state, including successful rogue disarms. */
int game_prop_trapped(const Game *g,unsigned i){
 if(g->area||i>=MAX_PROPS||(g->trap_spent&((uint64_t)1<<i))||g->props[i].state)return 0;
 if(game_wall_trap_armed(g,i))return 1;
 if(game_wall_trap_linked(g,i)||game_dungeon_depth()<=4)return 0;
 unsigned k=g->props[i].kind;if(k!=PROP_CHEST&&k!=PROP_CHEST_MEDIUM&&k!=PROP_CHEST_LARGE)return 0;
 return item_seed(campaign_seed^dungeon_level*0x9e3779b9u^(i+1)*0x74726170u)%100<10;
}
void game_trap_damage(Game *g,unsigned damage,unsigned element){
 if(g->hp<=0||g->area)return;
 if(item_effects.special_flags&ITEM_HALF_TRAP)damage>>=1;
 int adjusted=(int)damage+item_effects.damage_taken;if(adjusted<1)adjusted=1;
 if(element<3)adjusted-=adjusted*item_effects.resist[element]/100;
 unsigned hit=game_spell_absorb(g,(unsigned)adjusted);g->hp=hit>=(unsigned)g->hp?0:g->hp-hit;g->hit_flash=6;
}
