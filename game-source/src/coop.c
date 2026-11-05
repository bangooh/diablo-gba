#include "coop.h"
#include "item_effect_cache.h"
#include "world_rooms.h"
#include "shop_session.h"
CoopPlayer coop_players[4];
unsigned coop_cpu_mask;
static uint16_t cpu_dead_ticks[4],cpu_heal_ticks[4];
static uint16_t cpu_far_ticks[4],cpu_stuck_ticks[4];
unsigned coop_active,coop_id,coop_members,coop_turn,coop_ticks;
unsigned coop_campaign_active;
uint32_t coop_campaign_epoch;
CoopRegionRequest coop_region_request;
uint32_t coop_hash;
static int ai_player=-1;
uint8_t *coop_enemy_indices;
const Game *coop_enemy_game;
static unsigned coop_enemy_count,coop_enemy_dirty;
void coop_enemy_invalidate(void){coop_enemy_dirty=1;}
unsigned coop_enemy_total(const Game *g){
 if(!coop_enemy_indices||coop_enemy_game!=g)return MAX_ENEMIES;
 if(coop_enemy_dirty){coop_enemy_count=0;for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1)coop_enemy_indices[coop_enemy_count++]=(uint8_t)i;coop_enemy_dirty=0;}
 return coop_enemy_count;
}
void coop_region_clear(void){coop_region_request=(CoopRegionRequest){0};}
int coop_region_pending(void){return coop_region_request.pending;}
int coop_request_region(unsigned destination,int x,int y,int owner){
 if((coop_cpu_mask&(1u<<coop_turn))||!coop_active||!coop_campaign_active||destination>REGION_BONE||owner<-1||owner>3)return 0;
 if(coop_turn>=4||!(coop_members&(1u<<coop_turn)))return 0;
 int limit=destination?80:96;if((x!=-1||y!=-1)&&(x<0||y<0||x>=limit||y>=limit))return 0;
 if(!coop_region_request.pending)coop_region_request=(CoopRegionRequest){1,(uint8_t)destination,(uint8_t)coop_turn,(int8_t)owner,(int16_t)x,(int16_t)y};
 return 1;
}
void coop_capture(Game *g,unsigned id){
 CoopPlayer *p=&coop_players[id];game_get_character(g,&p->hero);
 p->x=g->x;p->y=g->y;p->kills=g->kills;p->move_wait=g->move_wait;p->attack_wait=g->attack_wait;p->attack_duration=g->attack_duration;p->reaction_duration=g->reaction_duration;p->reaction_block=g->reaction_block;
 p->flash=g->flash;p->hit_flash=g->hit_flash;p->facing=g->facing;p->attack_queued=g->attack_queued;
 p->notice=g->pickup_notice;p->notice_ticks=g->pickup_notice_ticks;p->target=g->target;p->swing=g->swing_target;
 p->town_message=g->town_message;
 p->damage_min=item_damage_min;p->damage_max=item_damage_max;p->armor=item_armor;p->weapon_flags=item_weapon_flags;
}
void coop_select(Game *g,unsigned id){
 const CoopPlayer *p=&coop_players[id];coop_turn=id;game_apply_character(g,&p->hero);
 g->x=p->x;g->y=p->y;g->kills=p->kills;g->move_wait=p->move_wait;g->attack_wait=p->attack_wait;g->attack_duration=p->attack_duration;g->reaction_duration=p->reaction_duration;g->reaction_block=p->reaction_block;
 g->flash=p->flash;g->hit_flash=p->hit_flash;g->facing=p->facing;g->attack_queued=p->attack_queued;
 g->pickup_notice=p->notice;g->pickup_notice_ticks=p->notice_ticks;g->target=p->target;g->swing_target=p->swing;
 g->town_message=p->town_message;
}
void game_spells_remove_owner(unsigned owner);
void game_arrows_remove_owner(unsigned owner);
void coop_set_members(Game *g,unsigned members){
 unsigned removed=coop_members&~members;
 for(unsigned i=0;i<4;i++)if(removed&(1u<<i)){
  coop_select(g,i);game_item_drag(g,127);game_spell_cancel(g);
  g->move_wait=g->attack_wait=g->attack_duration=g->attack_queued=0;g->target=g->swing_target=-1;
  coop_capture(g,i);coop_players[i].previous=0;
  game_spells_remove_owner(i);game_arrows_remove_owner(i);
 }
 coop_members=members;coop_cpu_mask&=members;ai_player=-1;coop_enemy_invalidate();
 coop_select(g,coop_id);coop_checksum(g);
}
int coop_blocks(int x,int y,int except){
 if(!coop_active)return 0;
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&(int)i!=except&&coop_players[i].hero.hp>0&&coop_players[i].x==x&&coop_players[i].y==y)return 1;
 return 0;
}
__attribute__((minsize,noinline)) int coop_movement_blocks(int x,int y,unsigned mover){
 if(!coop_active)return 0;
 if(mover>=4||(coop_cpu_mask&(1u<<mover)))return coop_blocks(x,y,(int)mover);
 for(unsigned i=0;i<4;i++)if((coop_members&~coop_cpu_mask&(1u<<i))&&i!=mover&&coop_players[i].hero.hp>0&&coop_players[i].x==x&&coop_players[i].y==y)return 1;
 return 0;
}
__attribute__((section(".iwram.coop"))) int coop_distance(int x,int y){
 int best=20000;
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&coop_players[i].hero.hp>0){int dx=x-coop_players[i].x,dy=y-coop_players[i].y,d=dx*dx+dy*dy;if(d<best)best=d;}
 return best;
}
void coop_ai_target(Game *g,int monster){
 if(!coop_active)return;
 if(ai_player>=0)coop_capture(g,(unsigned)ai_player);
 int best=20000,best_room=0;unsigned target=coop_turn;const Enemy *e=&g->enemies[monster];
 int room=game_room_at(e->x,e->y);
 /* Retail M_Enemy: same transparency room wins before Chebyshev distance;
  * equal-distance ties retain the first active player in stable slot order. */
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&coop_players[i].hero.hp>0){
  int dx=e->x-coop_players[i].x,dy=e->y-coop_players[i].y;if(dx<0)dx=-dx;if(dy<0)dy=-dy;int d=dx>dy?dx:dy;
  int same=room>=0&&room==game_room_at(coop_players[i].x,coop_players[i].y);
  if((same&&!best_room)||((same||!best_room)&&d<best)){best=d;best_room=same;target=i;}
 }
 if(ai_player!=(int)target)coop_select(g,target);ai_player=(int)target;
}
void coop_ai_finish(Game *g){if(ai_player>=0)coop_capture(g,(unsigned)ai_player);ai_player=-1;coop_select(g,coop_id);}
static uint32_t hash_bytes(uint32_t h,const void *v,unsigned n){const uint8_t *p=v;for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
/* Campaign metadata is a cold ROM helper; keep the existing hot world hash
 * within the fixed24KiB IWRAM code budget. */
static __attribute__((noinline)) uint32_t coop_world_checksum(uint32_t h,const Game *g){
 if(coop_cpu_mask){h=hash_bytes(h,&coop_cpu_mask,sizeof(coop_cpu_mask));h=hash_bytes(h,cpu_dead_ticks,sizeof(cpu_dead_ticks));h=hash_bytes(h,cpu_heal_ticks,sizeof(cpu_heal_ticks));}
 if(coop_cpu_mask){h=hash_bytes(h,cpu_far_ticks,sizeof(cpu_far_ticks));h=hash_bytes(h,cpu_stuck_ticks,sizeof(cpu_stuck_ticks));}
 h=hash_bytes(h,&coop_members,sizeof(coop_members));h=hash_bytes(h,&campaign_seed,sizeof(campaign_seed));h=hash_bytes(h,&dungeon_level,sizeof(dungeon_level));
 h=hash_bytes(h,&g->area,sizeof(g->area));h=hash_bytes(h,&g->won,sizeof(g->won));h=hash_bytes(h,&butcher_quest,sizeof(butcher_quest));
 h=hash_bytes(h,&quest_progress,sizeof(quest_progress));h=hash_bytes(h,&coop_campaign_epoch,sizeof(coop_campaign_epoch));h=hash_bytes(h,&coop_campaign_active,sizeof(coop_campaign_active));
 return game_shop_hash(h);
}
__attribute__((section(".iwram.coop"))) void coop_checksum(const Game *g){
 uint32_t h=2166136261u;h=hash_bytes(h,&game_difficulty,sizeof(game_difficulty));h=hash_bytes(h,g->map,sizeof(g->map));h=hash_bytes(h,g->enemies,sizeof(g->enemies));
 h=hash_bytes(h,g->drops,sizeof(g->drops));h=hash_bytes(h,g->props,sizeof(g->props));
 h=hash_bytes(h,game_corpses,sizeof(game_corpses));h=hash_bytes(h,&g->trap_spent,sizeof(g->trap_spent));
 h=hash_bytes(h,g->enemy_attack,sizeof(g->enemy_attack));h=hash_bytes(h,g->enemy_facing,sizeof(g->enemy_facing));
 h=hash_bytes(h,coop_players,sizeof(coop_players));h=hash_bytes(h,monster_credit,sizeof(monster_credit));coop_hash=game_monsters_hash(game_spells_hash(game_arrows_hash(h)));
 coop_hash=game_item_drag_hash(coop_world_checksum(coop_hash,g));
}
static int coop_cpu_yield(Game *g,unsigned id,const uint16_t keys[4]);
static void coop_cpu_recall(Game *g,unsigned id,int moved);
static void coop_sim_step_inner(Game *g,const uint16_t keys[4]){
 if(g->won||g->paused||coop_region_pending())return;
 g->ticks++;
 unsigned leader=0;for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&coop_players[i].hero.hp>0){leader=i;break;}
 coop_select(g,leader);
 if(game_finale_step(g)){coop_capture(g,leader);coop_select(g,coop_id);coop_checksum(g);coop_ticks++;return;}
 game_quest_npc_step(g);game_quest_enter(g);coop_capture(g,leader);game_props_step(g);
 for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){
  coop_select(g,i);unsigned held;
  if(coop_cpu_mask&(1u<<i)){coop_cpu_step(g,i);int yield=coop_cpu_yield(g,i,keys);held=yield<0?coop_cpu_input(g,i):(unsigned)yield;}
  else held=keys[i]&1023&~(KEY_START|KEY_SELECT);
  unsigned pressed=held&~coop_players[i].previous;
  if(coop_cpu_mask&(1u<<i))pressed&=KEY_L|KEY_R|KEY_B;
  int old_x=g->x,old_y=g->y;
  game_step(g,held,pressed);
  if(coop_cpu_mask&(1u<<i))coop_cpu_recall(g,i,g->x!=old_x||g->y!=old_y);
  if(!coop_region_pending())game_coop_actor_interaction(g,pressed);
  coop_capture(g,i);coop_players[i].previous=(uint16_t)held;
  if(coop_region_pending())break;
 }
 if(coop_region_pending()){
  /* Every input in this acknowledged frame is consumed, including actors
   * after the winning request. A held A must not reopen stairs on arrival. */
  for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i))coop_players[i].previous=keys[i]&1023&~(KEY_START|KEY_SELECT);
  coop_select(g,coop_id);coop_checksum(g);coop_ticks++;return;
 }
 game_arrows_step(g);game_activate_nearby(g);ai_player=-1;game_monsters_step(g);coop_ai_finish(g);
 coop_ticks++;
}

void coop_sim_step(Game *g,const uint16_t keys[4]){
 uint8_t indices[MAX_ENEMIES];coop_enemy_indices=indices;coop_enemy_game=g;coop_enemy_invalidate();
 item_effect_cache_step(1);coop_sim_step_inner(g,keys);item_effect_cache_step(0);coop_enemy_indices=0;coop_enemy_game=0;
}

#include "coop_cpu.inc"
#include "coop_cpu_yield.inc"
#include "coop_cpu_party.inc"
#include "coop_restart.inc"
#include "coop_cpu_recall.inc"
