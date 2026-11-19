/* Include the production implementation; unused paths are removed at link time. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HAZARD_ROM
#include "arrows.c"
ItemEffects item_effects;
HeroProgress hero_progress;
unsigned dungeon_level=9,coop_active,coop_turn,coop_members;
CoopPlayer coop_players[4];
const MonsterDef monster_defs[MONSTER_TYPES]={[0]={.intelligence=3}};
unsigned game_monster_level(unsigned k){(void)k;return 100;}
unsigned game_monster_base_level(unsigned k){(void)k;return 26;}
unsigned game_monster_accuracy(unsigned k,int secondary){(void)k;(void)secondary;return 100;}
unsigned game_monster_damage_bound(unsigned k,int secondary,int high){(void)k;(void)secondary;(void)high;return 30;}
unsigned game_armor(void){return 0;}
unsigned game_block_chance(const Game *g,unsigned level){(void)g;(void)level;return 0;}
unsigned game_recovery_duration(unsigned cls,int block){(void)cls;(void)block;return 4;}
unsigned game_hero_class(void){return 0;}
unsigned game_spell_absorb(Game *g,unsigned d){(void)g;return d;}
unsigned game_spell_absorb_q6(Game *g,unsigned d){(void)g;return d;}
void coop_capture(Game *g,unsigned p){coop_players[p].hero.hp=g->hp;}
void coop_select(Game *g,unsigned p){g->hp=coop_players[p].hero.hp;g->x=coop_players[p].x;g->y=coop_players[p].y;coop_turn=p;}
int game_walkable(const Game *g,int x,int y){(void)g;(void)x;(void)y;return 1;}
int game_monster_missile_target(const Game *g,int i){(void)g;(void)i;return 1;}
int game_finale_protected(unsigned k){(void)k;return 0;}
unsigned game_monster_immunities(unsigned k){(void)k;return 0;}
unsigned game_monster_armor(unsigned k){(void)k;return 0;}
int game_spell_stoned(unsigned i){(void)i;return 0;}
void game_damage_enemy_environment(Game *g,int i,int d,unsigned e){(void)g;(void)i;(void)d;(void)e;}
void game_spell_projectiles_step(Game *g){(void)g;}
unsigned game_bow_accuracy(unsigned p){(void)p;return 100;}
void item_build_effects(const HeroProgress *p,const Inventory *i,unsigned c,ItemEffects *e){(void)p;(void)i;(void)c;*e=item_effects;}
void game_weapon_hit(Game *g,unsigned i,unsigned d,unsigned f,unsigned l){(void)g;(void)i;(void)d;(void)f;(void)l;}
Inventory *game_inventory(void){static Inventory inv;return &inv;}
void game_monster_knockback(Game *g,unsigned i,int x,int y){(void)g;(void)i;(void)x;(void)y;}
int main(void){
 Game g={.hp=1000,.x=5,.y=5};hero_progress.level=1;
 for(unsigned spell=0;spell<3;spell++)for(unsigned resist=0;resist<=75;resist+=75){
  memset(&item_effects,0,sizeof item_effects);for(unsigned i=0;i<3;i++)item_effects.resist[i]=resist;
  unsigned id=spell==0?MONSTER_MISSILE_FLAME:spell==1?SPELL_LIGHTNING:MONSTER_MISSILE_ACID;
  Arrow a={.x=1280,.y=1280,.last_x=5,.last_y=5,.start_x=5,.start_y=5,.vx=-1,.vy=255,.owner=0,.reserved=0,.direction=HOSTILE_PROJECTILE|HOSTILE_BURST,.fire=id,.damage=spell==2?2:30};
  g.hp=1000;
  for(unsigned i=0;i<20;i++)assert(hostile_hit(&g,&a,5,5)==1);
  /* Native minimum64 Q6 precedes resistance. Acid uses magic resistance. */
  assert(1000-g.hp==(resist==0?20:5));
 }
 /* Direct acid remains whole-HP damage; it must not inherit puddle scaling. */
 Arrow acid={.x=1280,.y=1280,.last_x=5,.last_y=5,.start_x=5,.start_y=5,.owner=0,.reserved=0,.direction=HOSTILE_PROJECTILE,.fire=MONSTER_MISSILE_ACID,.damage=30};
 memset(&item_effects,0,sizeof item_effects);g.hp=1000;assert(hostile_hit(&g,&acid,5,5));assert(1000-g.hp>=30);
 hostile_burst(&g,&acid,1280,1280);assert(acid.damage==2&&acid.lightning==8&&acid.life>=168&&acid.life<=182);
 /* Independent per-player fractional carries: no loss or cross-player leakage. */
 Arrow flame=acid;flame.fire=MONSTER_MISSILE_FLAME;flame.damage=30;flame.vx=-1;flame.vy=255;
 item_effects.resist[0]=75;coop_active=1;coop_members=15;coop_turn=0;g.hp=1000;
 for(unsigned p=0;p<4;p++){coop_players[p].x=5;coop_players[p].y=5;coop_players[p].hero.hp=1000;}
 for(unsigned t=0;t<20;t++)assert(hostile_hit(&g,&flame,5,5)==4);
 for(unsigned p=0;p<4;p++)assert(coop_players[p].hero.hp==995);
 coop_active=0;
 /* Flame/bolt segments use Q6 attack damage and native lifetimes. */
 for(unsigned n=0;n<3;n++){
  memset(game_arrows,0,sizeof game_arrows);Arrow controller={.fire=MONSTER_MISSILE_FLAME,.lightning=n};
  lightning_segment(&controller,5,5);assert(game_arrows[0].life==20+5*n&&game_arrows[0].damage==30);
 }
 memset(game_arrows,0,sizeof game_arrows);Arrow controller={.fire=SPELL_LIGHTNING};
 lightning_segment(&controller,5,5);assert(game_arrows[0].life==10&&game_arrows[0].damage==60);
 /* Execute the real projectile loop: exactly three flame segments, no
  * direct controller damage, and no whole-HP hit during the acid splash. */
 memset(game_arrows,0,sizeof game_arrows);g.hp=1000;g.x=5;g.y=5;
 game_arrows[0]=(Arrow){.x=2*256,.y=5*256,.vx=256,.start_x=2,.start_y=5,.last_x=2,.last_y=5,.life=255,.direction=HOSTILE_PROJECTILE,.fire=MONSTER_MISSILE_FLAME,.damage=30};
 for(unsigned t=0;t<3;t++)game_arrows_step(&g);
 unsigned segments=0;for(unsigned i=0;i<MAX_ARROWS;i++)if(game_arrows[i].life)segments++;
 assert(segments==3&&!game_arrows[0].life&&g.hp>=999);
 memset(game_arrows,0,sizeof game_arrows);game_arrows[0]=acid;g.hp=1000;
 for(unsigned t=0;t<8;t++)game_arrows_step(&g);
 assert(g.hp==1000);game_arrows_step(&g);assert(g.hp==999);
 puts("Monster hazards: direct versus Q6 damage, 0/75% resistance, acid lifetime, four-player carries and segments PASS");
}
