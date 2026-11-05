#include "game.h"
#include "coop.h"
#include "hazards.h"
#define HAZARD_ROM __attribute__((section(".text.ui"),noinline))
/* PM_DoRangeAttack / AddArrow / MI_Arrow adaptation. Positions are Q8 world
 * cells. Four swept samples per20Hz tick prevent thin-wall tunnelling. */
Arrow game_arrows[MAX_ARROWS];
volatile unsigned bow_shots,bow_hits,bow_blocked,bow_misses;
static struct {int16_t x,y;uint8_t valid;} aim[4];
static uint32_t bow_rng=0x524f4755;
static int absolute(int x){return x<0?-x:x;}
static unsigned root(unsigned n){unsigned r=0;while((r+1)*(r+1)<=n)r++;return r?r:1;}
static unsigned hazard_roll(uint32_t *state,unsigned count){
 if(!*state)*state=0x524f4755u;*state^=*state<<13;*state^=*state>>17;*state^=*state<<5;
 return count?*state%count:0;
}
static unsigned trap_element(unsigned type){return type==GAME_TRAP_ARROW?0:type==GAME_TRAP_NOVA||type==GAME_TRAP_LIGHTNING?3:2;}
/* Retail PlayerMHit. Keep the Q6 damage roll through half-trap, get-hit and
 * resistance, then round once for this port's integer HP. Resistance in retail
 * precedes the block branch: positive resistance deliberately bypasses block. */
static HAZARD_ROM int hazard_player_q6(Game *g,unsigned kind,int trap,int physical,unsigned element,int shift,unsigned lo,unsigned hi,unsigned dist,int sx,int sy,uint32_t *rng,unsigned *remainder){
 if(g->hp<=0||(!trap&&kind>=MONSTER_TYPES)||hi<lo)return 0;
 unsigned hit=hazard_roll(rng,100),level=trap?hero_progress.level:game_monster_level(kind);
 int chance=physical?(trap?100-(int)(game_armor()/2)-2*(int)dist:(int)game_monster_accuracy(kind,0)+2*((int)level-hero_progress.level)+30-2*(int)dist-(int)game_armor()):(trap?40:40+2*((int)level-hero_progress.level)-2*(int)dist);
 int minimum=dungeon_level==16?30:dungeon_level==15?25:dungeon_level==14?20:10;if(chance<minimum)chance=minimum;
 unsigned block=game_block_chance(g,level),can_block=!g->area&&!g->move_wait&&!g->hit_flash&&!SPELL_CASTING(g->swing_target)&&!(item_effects.weapon_flags&ITEM_BOW)&&(item_effects.usable&(1u<<EQUIP_SHIELD));
 unsigned roll=can_block?hazard_roll(rng,100):100;if(shift)roll=100;
 if((int)hit>=chance)return 0;
 int damage=shift?(int)(lo+hazard_roll(rng,hi-lo+1)):(int)(lo*64+hazard_roll(rng,(hi-lo+1)*64));
 if(trap&&(item_effects.special_flags&ITEM_HALF_TRAP))damage/=2;
 damage+=item_effects.damage_taken*(shift?1:64);if(damage<64)damage=64;
 unsigned resist=element?item_effects.resist[element==1?2:element==2?0:1]:0;
 unsigned blocked=!resist&&roll<block;
 if(!blocked){damage-=damage*(int)resist/100;unsigned taken;
  if(remainder){unsigned q6=game_spell_absorb_q6(g,(unsigned)damage)+*remainder;taken=q6>>6;*remainder=q6&63;}
  else taken=game_spell_absorb(g,((unsigned)damage+63)/64);
  g->hp=taken>=(unsigned)g->hp?0:g->hp-taken;}
 if(!g->hp||resist||(!blocked&&(unsigned)damage<hero_progress.level*64u))return 1;
 if(blocked&&!trap){int dx=(sx>g->x)-(sx<g->x),dy=(sy>g->y)-(sy<g->y);
  static const int8_t vx[8]={1,0,-1,-1,-1,0,1,1},vy[8]={1,1,1,0,-1,-1,-1,0};
  for(unsigned d=0;d<8;d++)if(vx[d]==dx&&vy[d]==dy){g->facing=d;break;}
 }
 g->reaction_block=blocked;g->reaction_duration=game_recovery_duration(game_hero_class(),blocked);g->hit_flash=g->reaction_duration;
 g->attack_wait=g->attack_duration=0;if(g->attack_queued!=2)g->attack_queued=0;g->swing_target=-1;return 1;
}
static HAZARD_ROM int hazard_player(Game *g,unsigned kind,int trap,int physical,unsigned element,int shift,unsigned lo,unsigned hi,unsigned dist,int sx,int sy,uint32_t *rng){
 return hazard_player_q6(g,kind,trap,physical,element,shift,lo,hi,dist,sx,sy,rng,0);
}
/* Owner=-1 traps can hit either faction, but never add a hero XP contributor. */
HAZARD_ROM int game_trap_hit_cell(Game *g,int x,int y,unsigned min_mon,unsigned max_mon,unsigned min_hero,unsigned max_hero,unsigned dist,unsigned type,uint32_t *rng){
 if(!rng||type>=GAME_TRAP_COUNT||(unsigned)x>=MAP_W||(unsigned)y>=MAP_H||min_mon>max_mon||min_hero>max_hero)return 0;
 unsigned element=trap_element(type),shift=type==GAME_TRAP_LIGHTNING;int contacts=0;
 for(unsigned i=0;i<MAX_ENEMIES;i++){Enemy *e=&g->enemies[i];if(e->alive!=1||e->hp<=0||e->kind>=MONSTER_TYPES||e->x!=x||e->y!=y||!game_monster_missile_target(g,i)||game_finale_protected(e->kind))continue;
  if(element&&(game_monster_immunities(e->kind)&(1u<<(element-1))))continue;
  int chance=90-(int)game_monster_armor(e->kind)-(int)dist;if(chance<5)chance=5;if(chance>95)chance=95;
  unsigned roll=hazard_roll(rng,100);if((int)roll>=chance&&!game_spell_stoned(i))continue;
  unsigned damage=min_mon+hazard_roll(rng,max_mon-min_mon+1);if(shift)damage=(damage+63)/64;
  game_damage_enemy_environment(g,i,(int)damage,element);contacts++;
 }
 unsigned previous=coop_turn;if(coop_active)coop_capture(g,previous);
 for(unsigned p=0;p<(coop_active?4u:1u);p++){
  if(coop_active){if(!(coop_members&(1u<<p))||coop_players[p].hero.hp<=0||coop_players[p].x!=x||coop_players[p].y!=y)continue;coop_select(g,p);}
  if(g->x==x&&g->y==y)contacts+=hazard_player(g,0,1,type==GAME_TRAP_ARROW||type==GAME_TRAP_FARROW,element,shift,min_hero,max_hero,dist,x,y,rng);
  if(coop_active)coop_capture(g,p);
 }
 if(coop_active)coop_select(g,previous);return contacts;
}
void game_arrows_reset(void){
 for(int i=0;i<MAX_ARROWS;i++)game_arrows[i]=(Arrow){0};
 for(int i=0;i<4;i++)aim[i]=(typeof(aim[i])){0};
 game_spells_reset();bow_rng=0x524f4755;bow_shots=bow_hits=bow_blocked=bow_misses=0;
}
void game_bow_aim(const Game *g){
 static const int8_t dx[8]={1,0,-1,-1,-1,0,1,1},dy[8]={1,1,1,0,-1,-1,-1,0};
 unsigned owner=coop_active?coop_turn:0;int t=g->target;
 aim[owner].x=g->x+dx[g->facing]*16;aim[owner].y=g->y+dy[g->facing]*16;
 if(t>=0&&t<MAX_ENEMIES){aim[owner].x=g->enemies[t].x;aim[owner].y=g->enemies[t].y;}
 if(t<=-2&&t>=-65){aim[owner].x=g->props[-t-2].x;aim[owner].y=g->props[-t-2].y;}
 aim[owner].valid=1;
}
/* Same16-direction ordering as retail GetDirection16; dot product chooses
 * the closest world-grid direction without a floating point trig library. */
static unsigned direction16(int dx,int dy){
 static const int16_t vx[16]={181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237,256,237};
 static const int16_t vy[16]={181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98};
 int sx=dx,sy=dy,best=-2147483647;unsigned d=0;
 for(unsigned i=0;i<16;i++){int dot=sx*vx[i]+sy*vy[i];if(dot>best){best=dot;d=i;}}return d;
}
void game_bow_fire(Game *g){
 unsigned owner=coop_active?coop_turn:0;if(!aim[owner].valid||g->area)return;
 int dx=aim[owner].x-g->x,dy=aim[owner].y-g->y;aim[owner].valid=0;if(!dx&&!dy)return;
 for(int i=0;i<MAX_ARROWS;i++)if(!game_arrows[i].life){
  int sx=(dx-dy)*16,sy=(dx+dy)*8;unsigned len=root(sx*sx+sy*sy);
  unsigned speed=(item_effects.special_flags&ITEM_RANDOM_ARROW)?16+item_seed(g->ticks^bow_shots^(owner*0x85157af5u))%32:32;
  game_arrows[i]=(Arrow){g->x*256,g->y*256,dx*(int)(128*speed)/(int)len,dy*(int)(128*speed)/(int)len,g->x,g->y,g->x,g->y,64,owner,direction16(dx,dy),(item_effects.combat_flags&8)?1:0,game_weapon_damage(g),game_weapon_element_roll(g->ticks^bow_shots^(owner*0x85157af5u),1,0),game_weapon_element_roll(g->ticks^bow_shots^(owner*0x85157af5u),1,1)};bow_shots++;game_wear_equipment(g,WEAR_BOW);return;
 }
}
/* Bit6 distinguishes hostile physical arrows from player arrows and bit7 spells.
 * Snapshot kind/damage/source coordinates: a killed shooter need not remain alive. */
int game_monster_arrow(Game *g,unsigned enemy){
 if(g->area||enemy>=MAX_ENEMIES||g->enemies[enemy].alive!=1)return 0;
 const Enemy *e=&g->enemies[enemy];int dx=g->x-e->x,dy=g->y-e->y;if(!dx&&!dy)return 0;
 for(unsigned i=0;i<MAX_ARROWS;i++)if(!game_arrows[i].life){
  int sx=(dx-dy)*16,sy=(dx+dy)*8;unsigned len=root(sx*sx+sy*sy),low=game_monster_damage_bound(e->kind,0,0),high=game_monster_damage_bound(e->kind,0,1);
  bow_rng^=bow_rng<<13;bow_rng^=bow_rng>>17;bow_rng^=bow_rng<<5;
  game_arrows[i]=(Arrow){e->x*256,e->y*256,dx*4096/(int)len,dy*4096/(int)len,e->x,e->y,e->x,e->y,255,enemy,direction16(dx,dy)|64,e->kind,low+bow_rng%(high-low+1),0,0};return 1;
 }
 return 0;
}
/* Hostile spells share the bounded projectile pool. fire holds a spell ID only
 * when bit6 is set; player elemental arrows keep their existing damage fields. */
int game_monster_cast(Game *g,unsigned enemy,unsigned spell){
 if(g->area||enemy>=MAX_ENEMIES||g->enemies[enemy].alive!=1)return 0;
 if(spell!=SPELL_APOCALYPSE&&spell!=SPELL_FLASH){
  if(!game_monster_arrow(g,enemy))return 0;
  for(unsigned i=0;i<MAX_ARROWS;i++){Arrow *a=&game_arrows[i];if(a->life==255&&(a->direction&64)&&a->owner==enemy&&!a->fire){a->fire=spell;return 1;}}return 0;
 }
 for(unsigned i=0;i<MAX_ARROWS;i++)if(!game_arrows[i].life){const Enemy *e=&g->enemies[enemy];int x=spell==SPELL_FLASH?e->x:g->x,y=spell==SPELL_FLASH?e->y:g->y;
  /* Native AddFlash stores 2*mLevel in Q6; Flash2 keeps its initial4 Q6.
   * Four carry values start at63: floor((63+total damage)/64) equals this
   * cast's cumulative damage ceiling. Concurrent casts retain separate
   * rounding carries instead of adding global fractional-HP state. */
  game_arrows[i]=(Arrow){x*256,y*256,spell==SPELL_FLASH?-1:0,spell==SPELL_FLASH?255:0,e->x,e->y,x,y,spell==SPELL_FLASH?19:12,enemy,64,e->kind,spell==SPELL_FLASH?2*game_monster_level(e->kind):40,spell,0};return 1;
 }
 return 0;
}
HAZARD_ROM int game_trap_fire(Game *g,int sx,int sy,int tx,int ty,unsigned type,uint32_t seed){
 if(g->area||type>=GAME_TRAP_COUNT||(unsigned)sx>=MAP_W||(unsigned)sy>=MAP_H||(unsigned)tx>=MAP_W||(unsigned)ty>=MAP_H)return 0;
 static const int8_t vx[8]={1,0,-1,-1,-1,0,1,1},vy[8]={1,1,1,0,-1,-1,-1,0};
 unsigned depth=dungeon_level?dungeon_level:1,count=0,power=depth/2;
 if(type==GAME_TRAP_NOVA)power+=hazard_roll(&seed,3)+hazard_roll(&seed,3)+hazard_roll(&seed,3);
 for(unsigned ray=0;ray<(type==GAME_TRAP_NOVA?8u:1u);ray++){
  int slot=-1;for(unsigned i=0;i<MAX_ARROWS;i++)if(!game_arrows[i].life){slot=i;break;}if(slot<0)break;
  int dx=type==GAME_TRAP_NOVA?vx[ray]:tx-sx,dy=type==GAME_TRAP_NOVA?vy[ray]:ty-sy;if(!dx&&!dy){dx=vx[g->facing&7];dy=vy[g->facing&7];}
  int ix=(dx-dy)*16,iy=(dx+dy)*8;unsigned len=root(ix*ix+iy*iy),speed=type==GAME_TRAP_NOVA?16:type==GAME_TRAP_FIREBOLT?26:32;
  unsigned spell=type==GAME_TRAP_NOVA||type==GAME_TRAP_LIGHTNING?SPELL_LIGHTNING:type==GAME_TRAP_FIREBOLT?SPELL_FIREBOLT:0;
  game_arrows[slot]=(Arrow){sx*256,sy*256,dx*(int)(speed*128)/(int)len,dy*(int)(speed*128)/(int)len,sx,sy,sx,sy,255,HOSTILE_TRAP_OWNER,HOSTILE_PROJECTILE|direction16(dx,dy)|(type==GAME_TRAP_FARROW?HOSTILE_FARROW:0),type,depth,spell,power};count++;
 }
 return count;
}
static void hostile_magic(Game *g,const Arrow *a){
 if(g->hp<=0)return;unsigned damage=a->damage;
 int element=a->fire==SPELL_FIREBALL?0:a->fire==SPELL_APOCALYPSE?3:2;
 if(element<3)damage-=damage*item_effects.resist[element]/100;
 damage=game_spell_absorb(g,damage);g->hp=damage>=(unsigned)g->hp?0:g->hp-damage;g->hit_flash=6;
}
static HAZARD_ROM int hostile_hit(Game *g,Arrow *a,int x,int y){
 int dist=absolute(x-a->start_x);if(absolute(y-a->start_y)>dist)dist=absolute(y-a->start_y);
 if(a->owner==HOSTILE_TRAP_OWNER){unsigned type=a->reserved,lo=a->damage,hi=2*lo;
  if(type==GAME_TRAP_FARROW){lo+=hazard_roll(&bow_rng,10)+1;hi+=hazard_roll(&bow_rng,10)+1;if(hi<lo)hi=lo;if(!(a->direction&HOSTILE_BURST))type=GAME_TRAP_ARROW;}
  else if(type==GAME_TRAP_FIREBOLT)lo=hi=a->damage+hazard_roll(&bow_rng,2*a->damage);
  else if(type==GAME_TRAP_NOVA)lo=hi=a->lightning;
  else if(type==GAME_TRAP_LIGHTNING)lo=hi=a->lightning;
  return game_trap_hit_cell(g,x,y,lo,hi,lo,hi,dist,type,&bow_rng);
 }
 if(a->owner>=MAX_ENEMIES||a->reserved>=MONSTER_TYPES)return 0;
 int contacts=0;
 unsigned previous=coop_turn;if(coop_active)coop_capture(g,previous);
 for(unsigned p=0;p<(coop_active?4u:1u);p++){
  if(coop_active){if(!(coop_members&(1u<<p))||coop_players[p].hero.hp<=0||coop_players[p].x!=x||coop_players[p].y!=y)continue;coop_select(g,p);}
  if(g->hp<=0||g->x!=x||g->y!=y)continue;
  unsigned element=!a->fire?0:a->fire==SPELL_FIREBALL||a->fire==SPELL_FIREBOLT||a->fire==MONSTER_MISSILE_MAGMA||a->fire==MONSTER_MISSILE_FLAME?2:a->fire==SPELL_LIGHTNING?3:1;
  contacts+=hazard_player(g,a->reserved,0,!a->fire,element,0,a->damage,a->damage,dist,a->start_x,a->start_y,&bow_rng);
  if(coop_active)coop_capture(g,p);
 }
 if(coop_active)coop_select(g,previous);return contacts;
}
static HAZARD_ROM int hostile_line(const Game *g,int x,int y,int tx,int ty){
 int dx=absolute(tx-x),dy=absolute(ty-y),sx=(tx>x)-(tx<x),sy=(ty>y)-(ty<y),err=dx-dy;
 for(unsigned n=0;n<MAP_W+MAP_H;n++){
  if(x==tx&&y==ty)return 1;int ox=x,oy=y,e=2*err;if(e>-dy){err-=dy;x+=sx;}if(e<dx){err+=dx;y+=sy;}
  if(!game_walkable(g,x,y)||(x!=ox&&y!=oy&&(!game_walkable(g,x,oy)||!game_walkable(g,ox,y))))return 0;
 }return 0;
}
static HAZARD_ROM void hostile_burst(Game *g,Arrow *a,int old_x,int old_y){
 if(a->fire==SPELL_FIREBALL){
  int x=(a->x+128)>>8,y=(a->y+128)>>8;
  /* Retail checks the source-to-cell path, including a second center contact
   * after a successful direct hit. No area checks occur on later visual ticks. */
  static const int8_t dx[9]={0,0,0,1,1,1,-1,-1,-1},dy[9]={0,1,-1,0,-1,1,0,1,-1};
  for(unsigned i=0;i<9;i++)if(hostile_line(g,a->start_x,a->start_y,x+dx[i],y+dy[i]))hostile_hit(g,a,x+dx[i],y+dy[i]);
 }else if(a->direction&HOSTILE_FARROW){
  /* MI_LArrow backs up one complete tick before the MagBlos phase. */
  a->x=old_x;a->y=old_y;a->fire=SPELL_FIREBOLT;
 }else if(a->fire==MONSTER_MISSILE_ACID||a->fire==MONSTER_MISSILE_FLAME){a->last_x=(a->x+128)>>8;a->last_y=(a->y+128)>>8;a->vx=a->vy=0;a->direction|=HOSTILE_BURST;a->life=a->fire==MONSTER_MISSILE_ACID?24:12;return;}else if(a->fire!=SPELL_FIREBOLT&&a->fire!=MONSTER_MISSILE_MAGMA){a->life=0;return;}
 a->last_x=(a->x+128)>>8;a->last_y=(a->y+128)>>8;a->vx=a->vy=0;a->direction|=HOSTILE_BURST;a->life=9;
 /* MagBlos has10 native frames; this compact impact adapter lasts len-1. */
}
static HAZARD_ROM void lightning_segment(const Arrow *controller,int x,int y){
 for(unsigned i=0;i<MAX_ARROWS;i++)if(&game_arrows[i]!=controller&&!game_arrows[i].life){Arrow *a=&game_arrows[i];*a=*controller;a->x=x*256;a->y=y*256;a->last_x=x;a->last_y=y;a->vx=a->vy=0;a->direction|=HOSTILE_BURST;a->life=8;a->lightning=2*a->damage+hazard_roll(&bow_rng,a->damage);return;}
}
static HAZARD_ROM void hostile_flash(Game *g,Arrow *a){
 if(a->reserved>=MONSTER_TYPES)return;
 uint32_t fractions=(uint16_t)a->vx|((uint32_t)(uint16_t)a->vy<<16);unsigned previous=coop_turn;
 if(coop_active)coop_capture(g,previous);
 for(unsigned p=0;p<(coop_active?4u:1u);p++){
  if(coop_active){if(!(coop_members&(1u<<p)))continue;coop_select(g,p);}
  if(g->hp>0&&absolute(g->x-a->last_x)<=1&&absolute(g->y-a->last_y)<=1){
   /* MI_Flash covers six cells; MI_Flash2 covers the three upper cells.
    * Their independent hit checks share no cells and run for19 ticks. */
   unsigned power=g->y<a->last_y?4:a->damage,remainder=(fractions>>(6*p))&63;
   hazard_player_q6(g,a->reserved,0,0,1,1,power,power,0,a->start_x,a->start_y,&bow_rng,&remainder);
   fractions=(fractions&~(63u<<(6*p)))|(remainder<<(6*p));
  }
  if(coop_active)coop_capture(g,p);
 }
 if(coop_active)coop_select(g,previous);
 a->vx=(int16_t)(uint16_t)fractions;a->vy=(int16_t)(uint16_t)(fractions>>16);
}
void game_arrows_step(Game *g){
 if(g->area)return;
 game_spell_projectiles_step(g);
 for(int i=0;i<MAX_ARROWS;i++){
  Arrow *a=&game_arrows[i];if(!a->life||(a->direction&128))continue;
  if((a->direction&(HOSTILE_PROJECTILE|HOSTILE_BURST))==(HOSTILE_PROJECTILE|HOSTILE_BURST)){
   if((a->direction&HOSTILE_FARROW)||a->fire==SPELL_LIGHTNING||a->fire==MONSTER_MISSILE_ACID||a->fire==MONSTER_MISSILE_FLAME)hostile_hit(g,a,a->last_x,a->last_y);
   --a->life;continue;
  }
  --a->life;
  if((a->direction&64)&&a->fire==SPELL_FLASH){hostile_flash(g,a);continue;}
  if((a->direction&64)&&a->fire==SPELL_APOCALYPSE){
   if(a->life==9){unsigned previous=coop_turn;if(coop_active)coop_capture(g,previous);
    for(unsigned p=0;p<(coop_active?4u:1u);p++){if(coop_active){if(!(coop_members&(1u<<p)))continue;coop_select(g,p);}
     if(g->x==a->last_x&&g->y==a->last_y)hostile_magic(g,a);if(coop_active)coop_capture(g,p);
    }if(coop_active)coop_select(g,previous);
   }continue;
  }
  int ox=a->x,oy=a->y,impact=0,hostile=!!(a->direction&HOSTILE_PROJECTILE);
  for(int step=1;step<=4&&(hostile||a->life)&&!impact;step++){
   a->x=ox+a->vx*step/4;a->y=oy+a->vy*step/4;
   int x=(a->x+128)>>8,y=(a->y+128)>>8;if(x==a->last_x&&y==a->last_y)continue;
   /* Wall emitters start inside solid terrain. Retail excludes their source
    * cell from collision; let the first diagonal sample emerge from that wall. */
   int emerging=a->owner==HOSTILE_TRAP_OWNER&&a->last_x==a->start_x&&a->last_y==a->start_y&&!game_walkable(g,a->start_x,a->start_y);
   int corner=!emerging&&x!=a->last_x&&y!=a->last_y&&(!game_walkable(g,x,a->last_y)||!game_walkable(g,a->last_x,y));
   if(!game_walkable(g,x,y)||corner){
    /* Native CheckMissileCol breaks _oBreak barrels for either faction and
     * traps. Chests and other containers never auto-open on projectile impact. */
    for(int j=0;j<MAX_PROPS;j++)if(!corner&&game_prop_is_barrel(g->props[j].kind)&&!g->props[j].state&&g->props[j].x==x&&g->props[j].y==y)game_open_prop(g,j);
    impact=1;a->life=0;bow_blocked++;break;
   }
   a->last_x=x;a->last_y=y;
   if(hostile){
    if(a->fire==SPELL_LIGHTNING&&!(a->direction&HOSTILE_BURST)){lightning_segment(a,x,y);continue;}
    if(hostile_hit(g,a,x,y)&&!(a->owner==HOSTILE_TRAP_OWNER&&a->reserved==GAME_TRAP_NOVA)){impact=1;a->life=0;}
    continue;
   }
   for(int j=0;j<MAX_ENEMIES;j++)if(g->enemies[j].alive==1&&g->enemies[j].x==x&&g->enemies[j].y==y&&game_monster_missile_target(g,j)){
    int dist=absolute(x-a->start_x);if(absolute(y-a->start_y)>dist)dist=absolute(y-a->start_y);
    /* Rogue accuracy uses current dexterity + level +50 +class20, distance^2/2
     * and the target's difficulty-adjusted armor. */
    int chance=(int)game_bow_accuracy(a->owner)-dist*dist/2-(int)game_monster_armor(g->enemies[j].kind);if(a->direction&128){const HeroProgress *p=coop_active?&coop_players[a->owner].hero.progress:&hero_progress;unsigned cls=coop_active?(coop_players[a->owner].hero.reserved&255):game_hero_class();ItemEffects effects;item_build_effects(p,coop_active?&coop_players[a->owner].hero.inventory:game_inventory(),cls,&effects);chance=effects.stats[1]-2*(int)game_monster_level(g->enemies[j].kind)+50+(cls==2?20:0);}
    if(chance>95)chance=95;if(chance<5)chance=5;
    bow_rng^=bow_rng<<13;bow_rng^=bow_rng>>17;bow_rng^=bow_rng<<5;
    if((int)(bow_rng%100)>=chance){bow_misses++;break;}
    unsigned previous=coop_turn;if(coop_active)coop_select(g,a->owner);
    game_weapon_hit(g,j,a->damage,a->fire,a->lightning);if(a->reserved&1)game_monster_knockback(g,j,a->start_x,a->start_y);if(coop_active){coop_capture(g,a->owner);coop_select(g,previous);}
    a->life=0;bow_hits++;break;
   }
  }
  if(hostile&&!a->life)hostile_burst(g,a,ox,oy);
 }
}
void game_arrows_remove_owner(unsigned owner){
 if(owner>=4)return;aim[owner].valid=0;
 for(unsigned i=0;i<MAX_ARROWS;i++)if(!(game_arrows[i].direction&HOSTILE_PROJECTILE)&&game_arrows[i].owner==owner)game_arrows[i]=(Arrow){0};
}
uint32_t game_arrows_hash(uint32_t h){
 const uint8_t *p=(const uint8_t*)game_arrows;for(unsigned i=0;i<sizeof(game_arrows);i++)h=(h^p[i])*16777619u;
 p=(const uint8_t*)aim;for(unsigned i=0;i<sizeof(aim);i++)h=(h^p[i])*16777619u;
 return (h^bow_rng)*16777619u;
}
