#include "game.h"
#include "coop.h"
#include "world_rooms.h"
/* Retail spelldat.cpp IDs are preserved, including holes for unused spells.
 * book=0 is not obtainable from a book. No Hellfire-only spells are exposed. */
const SpellDef spell_defs[SPELL_MAX+1]={
 [0]={"EMPTY",0,0,0,0,0,0,0,0,0,0},
 [1]={"FIREBOLT",6,1,3,15,1,2,0,1,40,80},[2]={"HEALING",5,3,1,17,1,1,1,1,20,40},
 [3]={"LIGHTNING",10,1,6,20,4,3,0,3,20,60},[4]={"FLASH",30,2,16,33,5,3,0,4,20,40},
 [6]={"FIRE WALL",28,2,16,27,3,2,0,2,8,16},
 [7]={"TOWN PORTAL",35,3,18,20,3,1,0,3,8,12},
 [9]={"INFRAVISION",40,5,20,36,0,0,0,0,0,0},
 [8]={"STONE CURSE",60,3,40,51,6,1,0,5,8,16},
 [10]={"PHASING",12,2,4,39,7,1,0,6,40,80},[11]={"MANA SHIELD",33,0,33,25,6,1,0,5,4,10},
 [12]={"FIREBALL",16,1,10,48,8,2,0,7,40,80},[13]={"GUARDIAN",50,2,30,61,9,2,0,8,16,32},
 [14]={"CHAIN LIGHTNING",30,1,18,54,8,3,0,7,20,60},[15]={"FLAME WAVE",35,3,20,54,9,2,0,8,20,40},
 [18]={"NOVA",60,3,35,87,0,3,0,10,16,32},[20]={"INFERNO",11,1,6,20,3,2,0,2,20,40},
 [21]={"GOLEM",100,6,60,81,11,0,0,9,16,32},
 [23]={"TELEPORT",35,3,15,105,14,1,0,12,16,32},[24]={"APOCALYPSE",150,6,90,149,0,0,0,15,8,12},
 [29]={"ELEMENTAL",35,2,20,68,8,2,0,6,20,60},[30]={"CHARGED BOLT",6,1,6,25,1,3,0,1,40,80},
 [31]={"HOLY BOLT",7,1,3,20,1,1,0,1,40,80},[32]={"RESURRECT",20,0,20,30,0,1,1,5,4,10},
 [33]={"TELEKINESIS",15,2,8,33,2,1,0,2,20,40},[34]={"HEAL OTHER",5,3,1,17,1,1,1,1,20,40},
 [35]={"BLOOD STAR",25,2,14,70,14,1,0,13,20,60},[36]={"BONE SPIRIT",24,1,12,34,9,1,0,7,20,60}
};
uint8_t spell_pending[4];
volatile unsigned spell_casts,spell_failures,spell_audio_event;
static uint32_t spell_rng=0x86123456;
/* Cast properties are shared; only compact motion belongs to each bolt. */
SpellCast game_spell_casts[MAX_SPELL_CASTS];
SpellBolt game_spell_bolts[MAX_SPELL_BOLTS];
_Static_assert(sizeof(SpellCast)==8&&sizeof(SpellBolt)==12,"compact spell pool");
/* 0 mana, 1..47 pinned scroll, 129..175 pinned staff, 255 released. */
static uint8_t shield[4],cast_source[4],source_kind[4],effect_mask;
static uint32_t source_seed[4];
static uint8_t cast_level[4];
static uint16_t cast_cost[4];
/* A portal's owner is its stable player slot:12 bytes total. */
static struct {uint8_t floor,x,y;} portal[4];
static const int8_t dx8[8]={1,0,-1,-1,-1,0,1,1},dy8[8]={1,1,1,0,-1,-1,-1,0};
static int abs_i(int v){return v<0?-v:v;}
static int sign(int v){return (v>0)-(v<0);}
static unsigned owner_id(void){return coop_active?coop_turn:0;}
static unsigned random_n(unsigned n){spell_rng^=spell_rng<<13;spell_rng^=spell_rng>>17;spell_rng^=spell_rng<<5;return n?spell_rng%n:0;}
static unsigned scale(unsigned damage,unsigned level){while(level--)damage+=damage>>3;return damage>65535?65535:damage;}
int game_spell_supported(unsigned s){return s>0&&s<=SPELL_MAX&&spell_defs[s].name!=0;}
int game_spell_choice_valid(unsigned s){return !s||s==SPELL_STAFF||s==SPELL_SKILL||game_spell_supported(s);}
unsigned game_spell_id(unsigned choice){if(choice!=SPELL_STAFF)return game_spell_supported(choice)?choice:0;int i=game_staff_slot();return i<0?0:item_spell(&game_inventory()->items[i]);}
const char *game_spell_name(unsigned s){if(s==SPELL_SKILL){static const char *names[]={"ITEM REPAIR","DISARM TRAPS","STAFF RECHARGE"};return names[game_hero_class()];}unsigned id=game_spell_id(s);return id?spell_defs[id].name:s==SPELL_STAFF?"STAFF":"EMPTY";}
unsigned game_spell_level(const HeroProgress *p,unsigned s){
 if(!s||s>SPELL_MAX)return 0;if(s<=2)return p->spell_level[s-1];
 s-=3;return (p->spell_extra[s/2]>>((s&1)*4))&15;
}
/* Equipment changes casting power, never the learned/book level. */
unsigned game_spell_effective_level(unsigned s){
 if(!game_spell_supported(s))return 0;int level=(int)game_spell_level(&hero_progress,s)+game_spell_bonus();
 return level<0?0:level>255?255:(unsigned)level;
}
static void set_level(HeroProgress *p,unsigned s,unsigned n){
 if(s<=2){p->spell_level[s-1]=(uint8_t)n;return;}s-=3;unsigned shift=(s&1)*4;
 p->spell_extra[s/2]=(p->spell_extra[s/2]&~(15u<<shift))|(n<<shift);
}
void game_spell_grant(unsigned s){if(!game_spell_supported(s))return;unsigned n=game_spell_level(&hero_progress,s);if(n<15)set_level(&hero_progress,s,n+1);}
/* Exact item_scroll/item_spell predicate; keep the 47-slot scan local.
 * Stage56 host oracle covers every item-kind/flags byte and spell ID. */
static int scroll_slot(unsigned s){Inventory *bag=game_inventory();for(unsigned i=0;i<ITEM_SLOTS;i++)if((bag->items[i].kind==ITEM_BOOK_FIRE||bag->items[i].kind==ITEM_BOOK_HEAL)&&(bag->items[i].flags&128)&&(bag->items[i].flags&127)==s)return i;return -1;}
int game_spell_available(unsigned s){if(s==SPELL_SKILL)return 1;if(s==SPELL_STAFF){int i=game_staff_slot();return i>=0&&item_spell(&game_inventory()->items[i])&&game_inventory()->items[i].charges;}return game_spell_supported(s)&&((!(item_effects.special_flags&ITEM_NOMANA)&&game_spell_level(&hero_progress,s)&&game_spell_effective_level(s))||scroll_slot(s)>=0);}
unsigned game_spell_staff(uint32_t seed,unsigned level,int multiplayer){
 unsigned depth=level/2;if(!depth)depth=1;unsigned roll=seed%(SPELL_MAX+1)+1,s=0;
 /* GetStaffSpell walks native IDs, skipping the two co-op spells in solo. */
 while(roll){s=s%SPELL_MAX+1;if(spell_defs[s].staff_level&&spell_defs[s].staff_level<=depth&&(multiplayer||(s!=SPELL_RESURRECT&&s!=SPELL_HEAL_OTHER)))roll--;}
 return s;
}
int game_spell_shielded(unsigned player){return player<4&&shield[player];}
void game_spell_portal_consume(unsigned owner){if(owner<4)portal[owner].floor=portal[owner].x=portal[owner].y=0;}
void game_spell_new_campaign(void){for(unsigned p=0;p<4;p++)game_spell_portal_consume(p);}
int game_spell_portal_at(const Game *g,unsigned owner,int *x,int *y){
 static const uint8_t town[4][2]={{57,40},{59,40},{61,40},{63,40}};
 if(owner>=4||(!coop_active&&owner)||!portal[owner].floor||(coop_active&&!coop_campaign_active)||(!g->area&&dungeon_level!=portal[owner].floor))return 0;
 *x=g->area?town[owner][0]:portal[owner].x;*y=g->area?town[owner][1]:portal[owner].y;return 1;
}
int game_spell_portal_target(const Game *g){
 int best=-1,distance=4,x,y;
 for(unsigned p=0;p<4;p++)if(game_spell_portal_at(g,p,&x,&y)){
  int dx=abs_i(g->x-x),dy=abs_i(g->y-y);
  if(dx<=1&&dy<=1&&dx+dy<distance&&game_walkable(g,x,y)&&(!(dx&&dy)||(game_walkable(g,x,g->y)&&game_walkable(g,g->x,y)))){best=p;distance=dx+dy;}
 }
 return best;
}
int game_spell_portal_destination(unsigned owner){return owner<4?portal[owner].floor:0;}
int game_spell_portal_use(Game *g){
 if(g->attack_wait||g->move_wait||g->hp<=0)return 0;
 int owner=game_spell_portal_target(g);
 if(owner<0)return 0;
 if(coop_active){
  if(g->area)coop_request_region(portal[owner].floor,portal[owner].x,portal[owner].y,owner);
  else coop_request_region(0,58+owner*2,41,-1);
  return 1;
 }
 if(g->area){unsigned floor=portal[owner].floor;int tx=portal[owner].x,ty=portal[owner].y;if(!game_region_floor(g,floor))return 1;g->x=tx;g->y=ty;game_spell_portal_consume((unsigned)owner);}
 else if(game_region_town(g,0)){g->x=58+owner*2;g->y=41;}return 1;
}
unsigned game_spell_frames(unsigned cls){static const uint8_t frames[3]={20,16,12};return frames[cls<3?cls:0];}
unsigned game_spell_release(unsigned cls){static const uint8_t release[3]={14,12,8};return release[cls<3?cls:0];}
unsigned game_spell_cost(unsigned s){
 if(!game_spell_supported(s))return 0;
 const SpellDef *d=&spell_defs[s];int level=game_spell_effective_level(s),adj=(level?level-1:0)*d->adjust;
 if(s==SPELL_FIREBOLT)adj/=2;if(s==SPELL_RESURRECT)adj=(level?level-1:0)*2;
 int cost=d->mana-adj;if(s==SPELL_HEAL||s==SPELL_HEAL_OTHER)cost+=2*hero_progress.level;
 cost*=64;if(game_hero_class()==1)cost-=cost/4;
 return cost<d->minimum*64?d->minimum*64:(unsigned)cost;
}
unsigned game_spell_requirement(unsigned s){
 if(!game_spell_supported(s))return 255;unsigned req=spell_defs[s].magic;
 for(unsigned i=0;i<game_spell_level(&hero_progress,s);i++){req+=req/5;if(req>255)return 255;}return req;
}
int game_spell_learn(Game *g,unsigned s){
 if(!game_spell_supported(s)||!spell_defs[s].book||game_spell_level(&hero_progress,s)>=15||game_effective_stat(1)<game_spell_requirement(s))return 0;
 set_level(&hero_progress,s,game_spell_level(&hero_progress,s)+1);game_restore_mana(g,spell_defs[s].mana,0);return 1;
}
unsigned game_spell_book(uint32_t seed,unsigned depth){
 unsigned count=0;for(unsigned s=1;s<=SPELL_MAX;s++)if(spell_defs[s].book&&spell_defs[s].book<=depth)count++;
 if(!count)return 1;unsigned pick=seed%count;
 for(unsigned s=1;s<=SPELL_MAX;s++)if(spell_defs[s].book&&spell_defs[s].book<=depth){if(!pick--)return s;}return 1;
}
static int line_clear(const Game *g,int x,int y,int tx,int ty){
 int dx=abs_i(tx-x),dy=abs_i(ty-y),sx=sign(tx-x),sy=sign(ty-y),err=dx-dy;
 for(unsigned n=0;n<32;n++){
  if(x==tx&&y==ty)return 1;int ox=x,oy=y,e=err*2;if(e>-dy){err-=dy;x+=sx;}if(e<dx){err+=dx;y+=sy;}
  if(!game_walkable(g,x,y)||(x!=ox&&y!=oy&&(!game_walkable(g,x,oy)||!game_walkable(g,ox,y))))return 0;
 }return 0;
}
static int free_slot(void){for(int i=0;i<MAX_SPELL_BOLTS;i++)if(!game_spell_bolts[i].cast)return i;return -1;}
static int free_tile(const Game *g,int x,int y){return game_walkable(g,x,y)&&!game_monster_blocks(g,x,y,-1)&&!coop_blocks(x,y,-1);}
static int near_tile(const Game *g,int *x,int *y,unsigned radius){
 int ox=*x,oy=*y;
 for(int r=0;r<=(int)radius;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)
  if(free_tile(g,ox+dx,oy+dy)){*x=ox+dx;*y=oy+dy;return 1;}
 return 0;
}
static int nearest(const Game *g,int x,int y,unsigned radius){
 int best=-1,dist=10000;for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1&&!game_enemy_hidden(g,i)){int dx=g->enemies[i].x-x,dy=g->enemies[i].y-y,d=dx*dx+dy*dy;
  if(abs_i(dx)<=(int)radius&&abs_i(dy)<=(int)radius&&d<dist&&line_clear(g,x,y,g->enemies[i].x,g->enemies[i].y)){best=i;dist=d;}}
 return best;
}
static void target_tile(const Game *g,int *x,int *y){
 *x=g->x+dx8[g->facing]*6;*y=g->y+dy8[g->facing]*6;
 int t=game_ranged_target(g,0);if(t>=0){*x=g->enemies[t].x;*y=g->enemies[t].y;}
}
static int add_missile(Game *g,unsigned s,unsigned level,unsigned power,int x,int y,int tx,int ty,unsigned ticks,int *shared){
 int i=free_slot(),group=shared?*shared:-1;if(i<0)return 0;
 if(group<0){for(unsigned n=0;n<MAX_SPELL_CASTS;n++)if(!game_spell_casts[n].ticks){group=(int)n;break;}if(group<0)return 0;}
 int dx=tx-x,dy=ty-y;if(!dx&&!dy){dx=dx8[g->facing];dy=dy8[g->facing];}
 int sx=(dx-dy)*16,sy=(dx+dy)*8;unsigned root=1;while(root*root<(unsigned)(sx*sx+sy*sy))root++;
 unsigned speed=s==SPELL_FIREBOLT?8+level:s==SPELL_CHARGED?8:s==SPELL_LIGHTNING||s==SPELL_CHAIN?32:16;if(speed>40)speed=40;
 if(!game_spell_casts[group].ticks)game_spell_casts[group]=(SpellCast){ticks,power,s,level,owner_id(),0};
 game_spell_bolts[i]=(SpellBolt){x*256,y*256,dx*(int)(speed*256)/(int)root,dy*(int)(speed*256)/(int)root,x,y,(uint8_t)(group+1),-1};
 game_spell_casts[group].count++;if(shared)*shared=group;return i+1;
}
static void bolt_remove(SpellBolt *b){if(!b->cast)return;SpellCast *c=&game_spell_casts[b->cast-1];if(c->count&&!--c->count)*c=(SpellCast){0};*b=(SpellBolt){0};}
static unsigned damage_roll(unsigned s,unsigned level){
 unsigned pl=hero_progress.level;
 switch(s){
 case SPELL_FIREBOLT:return random_n(10)+(game_effective_stat(1)>>3)+level+1;
 case SPELL_HOLY:return random_n(10)+pl+9;
 case SPELL_FIREBALL:case SPELL_ELEMENTAL:return scale(2*(pl+random_n(10)+random_n(10))+4,level)/(s==SPELL_ELEMENTAL?2:1);
 case SPELL_BLOOD_STAR:return (game_effective_stat(1)/2)+3*level-(game_effective_stat(1)/8);
 case SPELL_BONE_SPIRIT:return 0;
 case SPELL_LIGHTNING:case SPELL_CHAIN:return random_n(pl)+random_n(pl)+2;
 case SPELL_CHARGED:return random_n(game_effective_stat(1)>>2)+1;
 case SPELL_NOVA:return scale((random_n(6)+random_n(6)+random_n(6)+random_n(6)+random_n(6)+pl+5)/2,level);
 case SPELL_GUARDIAN:return scale(random_n(10)+(pl>>1)+1,level);
 case SPELL_FIREWALL:return random_n(10)+random_n(10)+pl+2;
 case SPELL_INFERNO:return (3*(random_n(pl)+random_n(2)+2))/2;
 case SPELL_WAVE:return random_n(10)+pl+1;
 case SPELL_APOCALYPSE:{unsigned v=0;for(unsigned i=0;i<pl;i++)v+=random_n(6)+1;return v;}
 case SPELL_FLASH:{unsigned v=0;for(unsigned i=0;i<=pl;i++)v+=random_n(20)+1;return scale(v,level)*3/2;}
 default:return 0;
 }
}
static void hit(Game *g,int target,unsigned power,unsigned s){
 if(target<0||g->enemies[target].alive!=1)return;
 if(s==SPELL_GOLEM){if(g->enemies[target].kind==28&&game_fallen_retreat(target)==1)return;}
 else if(!game_monster_missile_target(g,target))return;
 if(s==SPELL_HOLY){if(!monster_undead[g->enemies[target].kind])return;game_damage_enemy(g,target,power);}
 else game_damage_enemy_element(g,target,power,spell_defs[s].element);
}
static void blast(Game *g,int x,int y,unsigned power,unsigned s,unsigned radius){
 for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1&&abs_i(g->enemies[i].x-x)<=(int)radius&&abs_i(g->enemies[i].y-y)<=(int)radius&&line_clear(g,x,y,g->enemies[i].x,g->enemies[i].y))hit(g,i,power,s);
}
/* Direct Fireball contact and every splash cell make independent hit rolls.
 * Other area spells retain OPT131's behavior in this release. */
static void fireball_blast(Game *g,int x,int y,unsigned power){
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1&&abs_i(g->enemies[i].x-x)<=1&&abs_i(g->enemies[i].y-y)<=1&&line_clear(g,x,y,g->enemies[i].x,g->enemies[i].y)&&game_monster_missile_target(g,i)){
  int chance=game_effective_stat(1)-2*(int)game_monster_level(g->enemies[i].kind)+50+(game_hero_class()==2?20:0);if(chance<5)chance=5;if(chance>95)chance=95;
  if(game_spell_stoned(i)||random_n(100)<(unsigned)chance)hit(g,i,power,SPELL_FIREBALL);
 }
}
#include "spell_effects.inc"
static unsigned heal_amount(unsigned level){
 unsigned amount=random_n(10)+1;
 for(unsigned i=0;i<hero_progress.level;i++)amount+=random_n(4)+1;
 for(unsigned i=0;i<level;i++)amount+=random_n(6)+1;
 unsigned cls=game_hero_class();return cls==0?amount*2:cls==1?amount+amount/2:amount;
}
static int other_player(const Game *g,int dead){
 int best=-1,dist=10000;if(!coop_active)return -1;
 for(unsigned i=0;i<4;i++)if(i!=coop_turn&&(coop_members&(1u<<i))&&((coop_players[i].hero.hp<=0)==dead)){
  int dx=coop_players[i].x-g->x,dy=coop_players[i].y-g->y,d=dx*dx+dy*dy;
  if(abs_i(dx)<=8&&abs_i(dy)<=8&&d<dist&&line_clear(g,g->x,g->y,coop_players[i].x,coop_players[i].y)){best=i;dist=d;}}
 return best;
}
int game_spell_player_target(const Game *g){
 if(!coop_active||g->hp<=0)return -1;
 unsigned owner=owner_id(),spell=spell_pending[owner];
 if(g->attack_wait&&SPELL_CASTING(g->swing_target)){
  /* The effect already happened. Do not preview another dead ally during
   * the recovery tail of the same resurrection animation. */
  if(cast_source[owner]==255)return -1;
  spell=SPELL_FROM_SWING(g->swing_target);
 }
 if(spell!=SPELL_HEAL_OTHER&&spell!=SPELL_RESURRECT)return -1;
 return other_player(g,spell==SPELL_RESURRECT);
}
/* Native DoResurrect/PlacePlayer: recover on a free nearby tile. */
static int resurrect_tile(const Game *g,const CoopPlayer *p,int *x,int *y){
 for(int radius=0;radius<=3;radius++)for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++){
  if(radius&&abs_i(dx)!=radius&&abs_i(dy)!=radius)continue;
  int xx=p->x+dx,yy=p->y+dy;
  if(game_walkable(g,xx,yy)&&!(xx==g->x&&yy==g->y)&&!coop_blocks(xx,yy,-1)&&!game_monster_blocks(g,xx,yy,-1)&&!game_spell_golem_at(xx,yy,0)){*x=xx;*y=yy;return 1;}
 }
 return 0;
}
static unsigned spell_life_cost(unsigned s){return s==SPELL_BLOOD_STAR?5:s==SPELL_BONE_SPIRIT?6:0;}
static unsigned spell_extra_mana(unsigned s){return shield[owner_id()]?spell_life_cost(s)*64:0;}
static int valid(const Game *g,unsigned s){
 if(!game_spell_supported(s)||g->hp<=0||(g->area&&!spell_defs[s].town))return 0;
 if(s==SPELL_TOWN&&coop_active&&!coop_campaign_active)return 0;
 if(!shield[owner_id()]&&(unsigned)g->hp<=spell_life_cost(s))return 0;
 if(s==SPELL_HEAL_OTHER||s==SPELL_RESURRECT)return other_player(g,s==SPELL_RESURRECT)>=0;
 return 1;
}
static int apply(Game *g,unsigned s,unsigned level){
 int tx,ty;target_tile(g,&tx,&ty);unsigned power=damage_roll(s,level);
 if(s==SPELL_INFRA){unsigned ticks=1584;for(unsigned i=0;i<level;i++)ticks+=ticks>>3;hero_progress.infra_ticks=ticks;return 1;}
 if(s==SPELL_HEAL){g->hp+=heal_amount(level);if(g->hp>=game_max_hp()){g->hp=game_max_hp();hero_progress.life_steal_fraction=0;}return 1;}
 if(s==SPELL_HEAL_OTHER||s==SPELL_RESURRECT){int p=other_player(g,s==SPELL_RESURRECT);if(p<0)return 0;
  CoopPlayer *other=&coop_players[p];if(s==SPELL_RESURRECT){int rx,ry;if(!resurrect_tile(g,other,&rx,&ry))return 0;other->x=rx;other->y=ry;shield[p]=0;spell_pending[p]=cast_source[p]=source_kind[p]=cast_level[p]=0;source_seed[p]=cast_cost[p]=0;other->attack_queued=other->attack_duration=other->reaction_duration=other->reaction_block=0;other->target=-1;other->hero.progress.life_steal_fraction=0;other->hero.hp=10;other->hero.mana=0;other->hero.progress.mana_fraction=0;other->hero.progress.mana_debt=0;other->attack_wait=other->move_wait=other->hit_flash=0;other->swing=-1;persistent_add(SPELL_RESURRECT,0,0,other->x,other->y,16);}
  else other->hero.hp+=heal_amount(level);
  if(other->hero.hp>=(int)game_character_max_hp(&other->hero)){other->hero.hp=game_character_max_hp(&other->hero);other->hero.progress.life_steal_fraction=0;}return 1;}
 if(s==SPELL_TOWN){int x=g->x+dx8[g->facing],y=g->y+dy8[g->facing];if(!near_tile(g,&x,&y,2))return 0;unsigned owner=owner_id();portal[owner].floor=dungeon_level;portal[owner].x=x;portal[owner].y=y;return 1;}
 if(s==SPELL_SHIELD){shield[owner_id()]=level?2:1;return 1;}
 if(s==SPELL_PHASING||s==SPELL_TELEPORT){
  if(s==SPELL_PHASING){int found=0;for(unsigned n=0;n<64;n++){tx=g->x+(int)random_n(13)-6;ty=g->y+(int)random_n(13)-6;if((tx!=g->x||ty!=g->y)&&free_tile(g,tx,ty)){found=1;break;}}if(!found)return 0;}
  else {tx=g->x+dx8[g->facing]*6;ty=g->y+dy8[g->facing]*6;if(!near_tile(g,&tx,&ty,5))return 0;}
  if(tx==g->x&&ty==g->y)return 0;g->x=tx;g->y=ty;g->move_wait=0;g->attack_queued=0;g->target=-1;return 1;
 }
 if(s==SPELL_TELEKINESIS){
  int drop=-1,dd=10000;for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active){int dx=g->drops[i].x-g->x,dy=g->drops[i].y-g->y,d=dx*dx+dy*dy;if(abs_i(dx)<=8&&abs_i(dy)<=8&&d<dd&&line_clear(g,g->x,g->y,g->drops[i].x,g->drops[i].y)){drop=i;dd=d;}}
  if(drop>=0)return game_item_pickup(g,drop);
  int best=-1,dist=10000;for(int i=0;i<MAX_PROPS;i++)if(game_prop_is_door(g->props[i].kind)||(!g->props[i].state&&g->props[i].kind&&(g->props[i].kind<PROP_DECOR_FIRST||game_prop_is_barrel(g->props[i].kind)||game_prop_is_shrine(g->props[i].kind)||game_prop_is_bookcase(g->props[i].kind)))){int dx=g->props[i].x-g->x,dy=g->props[i].y-g->y,d=dx*dx+dy*dy;
   /* End cell may be a solid chest/door; check the preceding tile. */
   if(abs_i(dx)<=8&&abs_i(dy)<=8&&d<dist&&line_clear(g,g->x,g->y,g->props[i].x-sign(dx),g->props[i].y-sign(dy))){best=i;dist=d;}}
  if(best>=0)return game_operate_prop(g,best);
  int t=game_ranged_target(g,0);if(t<0)return 0;Enemy *e=&g->enemies[t];int x=e->x+sign(e->x-g->x),y=e->y+sign(e->y-g->y);
  if(!free_tile(g,x,y))return 0;e->x=x;e->y=y;game_monster_telekinesis(g,t);return 1;
 }
 if(s==SPELL_APOCALYPSE){
  /* Diablo MI_Apoca: half-open16x16 scan. LineClear is Hellfire-only. */
  for(unsigned n=0;n<MAX_ENEMIES;n++){Enemy *e=&g->enemies[n];int dx=e->x-g->x,dy=e->y-g->y;
   if(e->alive!=1||dx<-8||dx>=8||dy<-8||dy>=8||!game_walkable(g,e->x,e->y)||!game_monster_missile_target(g,n))continue;
   apocalypse_queue(e->x,e->y);
   hit(g,n,power,s);
  }if(!apocalypse.ticks)apocalypse_step();return 1;
 }
 if(s==SPELL_FLASH){persistent_add(s,level,0,g->x,g->y,19);blast(g,g->x,g->y,power,s,1);return 1;}
 if(s==SPELL_STONE){int target=game_ranged_target(g,0);if(target<0||!game_monster_stonable(g,target)||g->enemies[target].kind==MONSTER_DIABLO||game_finale_protected(g->enemies[target].kind))return 0;
  int i=-1;for(unsigned j=0;j<PERSISTENT_SLOTS;j++)if(persistent[j].spell==s&&persistent[j].target==target){i=j;break;}
  if(i<0)i=persistent_add(s,level,0,g->enemies[target].x,g->enemies[target].y,(level+6<15?level+6:15)*16)-1;
  if(i<0)return 0;persistent[i].ticks=(level+6<15?level+6:15)*16;persistent[i].target=target;persistent[i].x=g->enemies[target].x;persistent[i].y=g->enemies[target].y;if(game_monster_ai(g->enemies[target].kind)!=MONSTER_AI_GARG){game_monster_reset(target);game_monster_reveal(g,target);g->enemy_attack[target]=0;}return 1;}
 if(s==SPELL_GOLEM){if(!near_tile(g,&tx,&ty,2)||!line_clear(g,g->x,g->y,tx,ty))return 0;
  int old=-1;for(unsigned j=0;j<PERSISTENT_SLOTS;j++)if(persistent[j].spell==s&&persistent[j].owner==owner_id())old=j;
  if(old>=0)persistent[old]=(PersistentSpell){0};
  int i=persistent_add(s,level,2*(level+4),tx,ty,65535)-1;if(i<0)return 0;persistent[i].hp=10*level+g->max_mana*2/3;return 1;}
 if(s==SPELL_GUARDIAN){
  int ox=tx,oy=ty,found=0;
  if(game_ranged_target(g,0)<0){ox=g->x+dx8[g->facing]*3;oy=g->y+dy8[g->facing]*3;}
  for(int r=0;r<=5&&!found;r++)for(int dy=-r;dy<=r&&!found;dy++)for(int dx=-r;dx<=r;dx++){
   int x=ox+dx,y=oy+dy,px=x-g->x,py=y-g->y;
   if((!px&&!py)||abs_i(px-py)>6||abs_i(px+py)>5||!free_tile(g,x,y)||!line_clear(g,g->x,g->y,x,y))continue;
   int occupied=0;for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell&&persistent[i].x==x&&persistent[i].y==y)occupied=1;
   if(!occupied){tx=x;ty=y;found=1;break;}
  }
  if(!found)return 0;
  unsigned ticks=(level+(hero_progress.level>>1))*16;if(ticks<30)ticks=30;if(ticks>480)ticks=480;
  return persistent_add(s,level,power,tx,ty,ticks)!=0;}
 if(s==SPELL_NOVA)return nova_add(g,power);
 if(s==SPELL_FIREWALL||s==SPELL_INFERNO){
  int x=s==SPELL_FIREWALL?tx:g->x+dx8[g->facing],y=s==SPELL_FIREWALL?ty:g->y+dy8[g->facing];
  if(s==SPELL_FIREWALL){int found=0,ox=x,oy=y;
   if(game_ranged_target(g,0)<0){ox=g->x+dx8[g->facing]*3;oy=g->y+dy8[g->facing]*3;}
   for(int r=0;r<=5&&!found;r++)for(int dy=-r;dy<=r&&!found;dy++)for(int dx=-r;dx<=r;dx++){
    int xx=ox+dx,yy=oy+dy;if((xx!=g->x||yy!=g->y)&&game_walkable(g,xx,yy)&&line_clear(g,g->x,g->y,xx,yy)){x=xx;y=yy;found=1;break;}}
   if(!found)return 0;}
  unsigned count=0;
  for(int n=0;n<5;n++){int k=n?((n+1)/2)*(n&1?1:-1):0;
   int xx=x+(s==SPELL_FIREWALL?-dy8[g->facing]*k:dx8[g->facing]*n),yy=y+(s==SPELL_FIREWALL?dx8[g->facing]*k:dy8[g->facing]*n);
   if(!game_walkable(g,xx,yy)||!line_clear(g,x,y,xx,yy)){if(s==SPELL_INFERNO)break;continue;}
   if(s==SPELL_FIREWALL){if(!persistent_add(s,level,power,xx,yy,(10+level)*16))break;}
   else{int i=add_missile(g,s,level,power,xx,yy,xx,yy,20+n*5,0)-1;if(i<0)break;game_spell_bolts[i].vx=game_spell_bolts[i].vy=0;}count++;
  }return count!=0;
 }
 if(s==SPELL_CHAIN){unsigned count=0;int group=-1;for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1&&!game_enemy_hidden(g,i)&&abs_i(g->enemies[i].x-g->x)<=8&&abs_i(g->enemies[i].y-g->y)<=8&&line_clear(g,g->x,g->y,g->enemies[i].x,g->enemies[i].y)){
   if(!add_missile(g,s,level,power,g->x,g->y,g->enemies[i].x,g->enemies[i].y,50,&group))break;if(++count==8)break;}
 return count!=0;}
 if(s==SPELL_WAVE){int group=-1;unsigned count=0;int side=level/2+2;if(side>7)side=7;
  for(int n=0;n<=side*2;n++){int k=n?((n+1)/2)*(n&1?1:-1):0;
   int x=g->x+dx8[g->facing]-dy8[g->facing]*k,y=g->y+dy8[g->facing]+dx8[g->facing]*k;
   if(!free_tile(g,x,y)||!line_clear(g,g->x+dx8[g->facing],g->y+dy8[g->facing],x,y))continue;
   if(!add_missile(g,s,level,power,x,y,x+dx8[g->facing]*8,y+dy8[g->facing]*8,64,&group))break;count++;
  }return count!=0;}
 if(s==SPELL_CHARGED){int group=-1;unsigned count=0,n=4+level/2;if(n>MAX_SPELL_BOLTS)n=MAX_SPELL_BOLTS;
  for(unsigned i=0;i<n;i++){int direction=((int)g->facing+(int)i-(int)n/2+8)&7;
   if(!add_missile(g,s,level,power,g->x,g->y,g->x+dx8[direction]*8,g->y+dy8[direction]*8,64,&group))break;count++;}
 return count!=0;}
 if(!add_missile(g,s,level,power,g->x,g->y,tx,ty,100,0))return 0;
 return 1;
}
/* Shrine missiles use the existing spell simulation and owner, with no
 * player casting animation, scroll consumption or mana charge. */
int game_spell_shrine_effect(Game *g,unsigned s,unsigned level){
 if(g->area||g->hp<=0||(s!=SPELL_SHIELD&&s!=SPELL_NOVA))return 0;
 if(!apply(g,s,level))return 0;
 if(!coop_active||coop_turn==coop_id)spell_audio_event=((spell_audio_event+256)&~255u)|s;
 return 1;
}
static int resource_valid(const Game *g,unsigned s,unsigned source){
 unsigned mana=g->mana*64+hero_progress.mana_fraction,extra=spell_extra_mana(s);
 if(mana<extra)return 0;
 if(!source)return !(item_effects.special_flags&ITEM_NOMANA)&&game_spell_level(&hero_progress,s)&&game_spell_effective_level(s)&&mana>=game_spell_cost(s)+extra;
 unsigned index=(source&127)-1;if(index>=ITEM_SLOTS)return 0;
 const Item *it=&game_inventory()->items[index];
 if(item_spell(it)!=s)return 0;
 if(source&128)return game_staff_slot()==(int)index&&it->charges;
 return item_scroll(it);
}
/* Mirror request's actual mana -> scroll fallback and equipped-staff rules.
 * Keep a known but unavailable spell visible with the original gray ramp. */
unsigned game_spell_icon_key(const Game *g,unsigned choice){
 if(choice==SPELL_SKILL){static const uint8_t icons[]={26,28,27};return icons[game_hero_class()]|((g->hp>0?SPELL_ICON_SKILL:SPELL_ICON_INVALID)<<8);}
 int index=choice==SPELL_STAFF?game_staff_slot():-1;
 unsigned s=choice==SPELL_STAFF?(index>=0?item_spell(&game_inventory()->items[index]):0):game_spell_supported(choice)?choice:0;
 unsigned type=SPELL_ICON_INVALID;
 if(valid(g,s)){
  if(choice==SPELL_STAFF){if(index>=0&&resource_valid(g,s,128u|(unsigned)(index+1)))type=SPELL_ICON_STAFF;}
  else if(resource_valid(g,s,0))type=SPELL_ICON_LEARNED;
  else {int scroll=scroll_slot(s);if(scroll>=0&&resource_valid(g,s,scroll+1))type=SPELL_ICON_SCROLL;}
 }
 return s|(type<<8);
}
static int source_valid(const Game *g,unsigned s){
 unsigned owner=owner_id(),source=cast_source[owner];
 if(!source&&SPELL_CASTING(g->swing_target))return !(item_effects.special_flags&ITEM_NOMANA)&&game_spell_level(&hero_progress,s)&&(unsigned)(g->mana*64+hero_progress.mana_fraction)>=cast_cost[owner]+spell_extra_mana(s);
 if(!resource_valid(g,s,source))return 0;
 if(source){const Item *it=&game_inventory()->items[(source&127)-1];if(it->seed!=source_seed[owner]||it->kind!=source_kind[owner])return 0;}
 return 1;
}
static __attribute__((section(".text.ui"),noinline)) int request_failure(Game *g,unsigned choice,unsigned s,unsigned source){
 spell_failures++;
 /* Use native CheckPlrSpell's class35 line only for the requested mana
  * shortfall. A scroll fallback, empty staff, unknown/town spell, zero spell
  * level, or an input during attack/recovery is a different failure. */
 if(source==255&&choice!=SPELL_STAFF&&!g->attack_wait&&!g->hit_flash&&valid(g,s)&&
    game_spell_level(&hero_progress,s)&&game_spell_effective_level(s)&&
    (unsigned)(g->mana*64+hero_progress.mana_fraction)<game_spell_cost(s))
  game_hero_error(g,HERO_ERROR_MANA);
 return 0;
}
static int request_source(Game *g,unsigned choice,unsigned s,unsigned source){
 /* game_step decrements the clock before reading B. Retire the old completed
  * cast before installing a new request, or control would cancel both. */
 if(!g->attack_wait&&SPELL_CASTING(g->swing_target))game_spell_cancel(g);
 if(g->attack_wait||g->hit_flash||!valid(g,s)||!resource_valid(g,s,source))return request_failure(g,choice,s,source);
 unsigned owner=owner_id();cast_source[owner]=source;source_seed[owner]=source_kind[owner]=0;cast_level[owner]=cast_cost[owner]=0;
 if(source){const Item *it=&game_inventory()->items[(source&127)-1];source_seed[owner]=it->seed;source_kind[owner]=it->kind;}
 spell_pending[owner]=s;hero_progress.spell_selected=choice;g->attack_queued=0;return 1;
}
int game_spell_request(Game *g,unsigned choice){
 if(choice==SPELL_SKILL){
  if(game_hero_class()==1)return game_skill_item(g,ITEM_SLOTS)==INV_OK;
  if(g->hp<=0||g->hit_flash||g->attack_wait)return 0;
  spell_pending[owner_id()]=SPELL_SKILL;g->attack_queued=0;return 1;
 }
 unsigned s=game_spell_id(choice),source=0;
 if(choice==SPELL_STAFF){int index=game_staff_slot();source=index<0?255:128u|(unsigned)(index+1);}
 else if(!resource_valid(g,s,0)){int index=scroll_slot(s);source=index<0?255:(unsigned)(index+1);}
 return request_source(g,choice,s,source);
}
int game_spell_request_scroll(Game *g,unsigned index){
 if(index>=ITEM_SLOTS)return 0;const Item *it=&game_inventory()->items[index];
 return item_scroll(it)&&request_source(g,item_spell(it),item_spell(it),index+1);
}
void game_spell_cancel(Game *g){
 unsigned owner=owner_id();spell_pending[owner]=cast_source[owner]=source_kind[owner]=0;source_seed[owner]=0;cast_level[owner]=cast_cost[owner]=0;
 if(SPELL_CASTING(g->swing_target)){g->attack_wait=0;g->swing_target=-1;}
}
/* Revive only the selected actor; living recalls retain Mana Shield. */
void game_spell_revive(Game *g){game_spell_cancel(g);shield[owner_id()]=0;}
/* Telekinesis can trigger a shrine that refills mana, recharges its casting
 * staff or fills an inventory slot. Pay before that effect, restoring the
 * exact resource on failure; the validated source must not be consumed twice. */
static int release_effect(Game *g,unsigned s,unsigned level,unsigned cost,unsigned source){
 Item *it=source?&game_inventory()->items[(source&127)-1]:0,before={0};
 int hp=g->hp,mp=g->mana;unsigned fraction=hero_progress.mana_fraction,debt=hero_progress.mana_debt;
 if(it){before=*it;if(source&128){if(!(coop_active&&(coop_cpu_mask&(1u<<owner_id()))))it->charges--;}else *it=(Item){.kind=ITEM_EMPTY};}
 unsigned mana=mp*64+fraction-(source?0:cost)-spell_extra_mana(s);
 g->mana=mana/64;hero_progress.mana_fraction=mana%64;
 if(!shield[owner_id()])g->hp-=spell_life_cost(s);
 if(apply(g,s,level))return 1;
 if(it)*it=before;g->hp=hp;g->mana=mp;hero_progress.mana_fraction=fraction;hero_progress.mana_debt=debt;return 0;
}
int game_spell_control(Game *g){
 unsigned owner=owner_id(),s=spell_pending[owner],cls=game_hero_class();
 if(SPELL_CASTING(g->swing_target)){
  s=SPELL_FROM_SWING(g->swing_target);
  if(g->hit_flash||g->hp<=0||!g->attack_wait){game_spell_cancel(g);return 1;}
  if(s==SPELL_SKILL)return 1; /* Repair/recharge opens its picker after the full cast. */
  if(g->attack_wait==game_spell_frames(cls)-game_spell_release(cls)+1&&cast_source[owner]!=255){
   unsigned level=cast_level[owner],cost=cast_cost[owner],source=cast_source[owner];
   int ready=valid(g,s)&&source_valid(g,s);cast_level[owner]=cast_cost[owner]=0;cast_source[owner]=255;source_seed[owner]=source_kind[owner]=0;
   if(!ready||!release_effect(g,s,level,cost,source)){spell_failures++;return 1;}
   spell_casts++;
  }return 1;
 }
 if(!s)return 0;if(g->hit_flash||g->hp<=0){game_spell_cancel(g);return 1;}if(g->move_wait)return 1;
 spell_pending[owner]=0;
 if(s==SPELL_SKILL){
  g->swing_target=SPELL_SKILL_SWING;g->attack_wait=game_spell_frames(cls);g->attack_queued=0;
  if(!coop_active||coop_turn==coop_id)spell_audio_event=((spell_audio_event+256)&~255u)|SPELL_SKILL;
  return 1;
 }
 if(!valid(g,s)||!source_valid(g,s)){game_spell_cancel(g);spell_failures++;return 1;}
 if(!spell_defs[s].town){g->target=(int8_t)game_ranged_target(g,0);if(g->target>=0){int dx=g->enemies[g->target].x-g->x,dy=g->enemies[g->target].y-g->y;
  if(dx>0)g->facing=dy>0?0:dy<0?6:7;else if(dx<0)g->facing=dy>0?2:dy<0?4:3;else g->facing=dy>0?1:5;}}
 cast_level[owner]=game_spell_effective_level(s);cast_cost[owner]=game_spell_cost(s);
 g->swing_target=(int8_t)(-67-(int)s);g->attack_wait=game_spell_frames(cls);g->attack_queued=0;
 if(!coop_active||coop_turn==coop_id)spell_audio_event=((spell_audio_event+256)&~255u)|s;return 1;
}
/* Kept for physical-arrow callers: player magic no longer lives there. */
unsigned game_spell_missile(unsigned i){(void)i;return 0;}
unsigned game_spell_bolt_world(unsigned i,unsigned phase,int *wx,int *wy){
 if(i>=MAX_SPELL_BOLTS||!game_spell_bolts[i].cast)return 0;
 const SpellBolt *b=&game_spell_bolts[i];const SpellCast *c=&game_spell_casts[b->cast-1];
 int x=b->x,y=b->y;if(!coop_active&&c->spell!=SPELL_INFERNO){x+=b->vx*(int)phase/3;y+=b->vy*(int)phase/3;}
 *wx=DUNGEON_ORIGIN+(x-y)/16;*wy=72+(x+y)/32;return c->spell;
}
/* PosOkMonst/2/3 queries the independent stationary pool at exact cells.
 * Actor immunity belongs to the AI caller; Inferno is not a Firewall. */
__attribute__((section(".text.ui"),noinline)) int game_spell_firewall_at(int x,int y){
 if(!(effect_mask&4)||(unsigned)x>=MAP_W||(unsigned)y>=MAP_H)return 0;
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_FIREWALL&&persistent[i].x==x&&persistent[i].y==y)return 1;return 0;
}
int game_spell_stoned(unsigned monster){
 if(!(effect_mask&1))return 0;
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_STONE&&persistent[i].target==(int)monster)return 1;return 0;
}
unsigned game_spell_absorb(Game *g,unsigned damage){
 unsigned p=owner_id();if(!shield[p]||g->hp<=0)return damage;
 unsigned reduced=damage;if(shield[p]>1)reduced-=reduced/3;
 unsigned mana=hero_progress.mana_debt?0:g->mana*64+hero_progress.mana_fraction;
 if(mana>=reduced*64){mana-=reduced*64;g->mana=mana/64;hero_progress.mana_fraction=mana%64;if(!mana)shield[p]=0;return 0;}
 shield[p]=0;g->mana=hero_progress.mana_fraction=0;return reduced-mana/64;
}
/* Flash keeps native Q6 damage through resistance and Mana Shield before
 * its projectile projects cumulative HP loss into this integer-HP runtime. */
unsigned __attribute__((section(".text.ui"),noinline)) game_spell_absorb_q6(Game *g,unsigned damage){
 unsigned p=owner_id();if(!shield[p]||g->hp<=0)return damage;
 unsigned reduced=damage;if(shield[p]>1)reduced-=reduced/3;
 unsigned mana=hero_progress.mana_debt?0:g->mana*64+hero_progress.mana_fraction;
 if(mana>=reduced){mana-=reduced;g->mana=mana/64;hero_progress.mana_fraction=mana%64;if(!mana)shield[p]=0;return 0;}
 shield[p]=0;g->mana=hero_progress.mana_fraction=0;return reduced-mana;
}
__attribute__((section(".text.ui"),noinline)) int game_spell_golem_position(unsigned owner,int *x,int *y){
 if(!(effect_mask&2))return 0;
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_GOLEM&&persistent[i].owner==owner){*x=persistent[i].x;*y=persistent[i].y;return 1;}
 return 0;
}
__attribute__((section(".text.ui"),noinline)) int game_spell_golem_at(int x,int y,unsigned damage){
 if(!(effect_mask&2))return 0;
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_GOLEM&&persistent[i].x==x&&persistent[i].y==y){
  if(damage){persistent[i].hp-=damage;if(persistent[i].hp<=0)persistent[i]=(PersistentSpell){0};}return 1;
 }
 return 0;
}
int game_spell_golem_hit(Game *g,unsigned monster,unsigned damage){
 if(!(effect_mask&2)||monster>=MAX_ENEMIES)return 0;
 /* M_Enemy's melee subset: same room first, then Chebyshev distance.
  * The already selected player wins equal-distance ties. Select before
  * checking the attack ray: an occluded nearer target must not cause an
  * attack on a different, lower-priority Golem. Ranged targeting is separate. */
 const Enemy *e=&g->enemies[monster];int room=game_room_at(e->x,e->y);
 int dx=abs_i(e->x-g->x),dy=abs_i(e->y-g->y),best=dx>dy?dx:dy;
 int same=room>=0&&room==game_room_at(g->x,g->y),target=-1;
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_GOLEM){
  const PersistentSpell *a=&persistent[i];int x=a->x,y=a->y;
  dx=abs_i(e->x-x);dy=abs_i(e->y-y);if(dx>=2||dy>=2)continue;
  int distance=dx>dy?dx:dy,in_room=room>=0&&room==game_room_at(x,y);
  if((in_room&&!same)||((in_room||!same)&&distance<best)){target=(int)i;best=distance;same=in_room;}
 }
 if(target<0)return 0;PersistentSpell *a=&persistent[target];
 if(!line_clear(g,e->x,e->y,a->x,a->y))return 0;
 if(damage){a->hp-=damage;if(a->hp<=0)*a=(PersistentSpell){0};}return 1;
}
void game_spell_projectiles_step(Game *g){
 effect_mask=0;for(unsigned i=0;i<PERSISTENT_SLOTS;i++){unsigned s=persistent[i].spell;if(s==SPELL_STONE)effect_mask|=1;if(s==SPELL_GOLEM)effect_mask|=2;if(s==SPELL_FIREWALL)effect_mask|=4;if(s==SPELL_GUARDIAN)effect_mask|=16;if(s==SPELL_FLASH||s==SPELL_APOCALYPSE||s==SPELL_RESURRECT||s==SPELL_FIREBALL)effect_mask|=32;}
 for(unsigned i=0;i<NOVA_SLOTS;i++)if(nova_waves[i].age)effect_mask|=8;
 if(apocalypse.count||apocalypse.ticks){apocalypse_step();effect_mask|=64;}
 if(effect_mask&55)persistent_step(g);if(effect_mask&8)nova_step(g);
 /* A stack-local cell hash preserves ascending monster order. Rebuild after
  * damage because death/knockback/teleport may move or spawn monsters. */
 uint8_t heads[64],next[MAX_ENEMIES];unsigned dirty=1,previous=coop_turn;
 for(unsigned group=0;group<MAX_SPELL_CASTS;group++)if(game_spell_casts[group].ticks)game_spell_casts[group].ticks--;
 for(unsigned i=0;i<MAX_SPELL_BOLTS;i++){
  SpellBolt *a=&game_spell_bolts[i];if(!a->cast)continue;SpellCast *m=&game_spell_casts[a->cast-1];unsigned s=m->spell;
  if(!m->ticks){bolt_remove(a);continue;}
  int x=(a->x+128)>>8,y=(a->y+128)>>8;
  if(s==SPELL_INFERNO){if(!(m->ticks%4)){effect_owner_enter(g,m->owner);blast(g,x,y,m->power,s,0);dirty=1;}continue;}
  if(s==SPELL_ELEMENTAL||s==SPELL_BONE_SPIRIT){
   int t=a->target;
   if(t<0||t>=MAX_ENEMIES||g->enemies[t].alive!=1||game_enemy_hidden(g,t)||abs_i(g->enemies[t].x-x)>8||abs_i(g->enemies[t].y-y)>8)t=nearest(g,x,y,8);
   a->target=t;if(t>=0){int dx=g->enemies[t].x*256-a->x,dy=g->enemies[t].y*256-a->y,max=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);if(max){a->vx=dx*192/max;a->vy=dy*192/max;}}
  }
  if(s==SPELL_CHARGED&&!(m->ticks%4)){int bend=(int)random_n(3)-1,v=a->vx;a->vx-=a->vy*bend/3;a->vy+=v*bend/3;}
  int ox=a->x,oy=a->y;
  for(int step=1;step<=4&&a->cast;step++){
   a->x=ox+a->vx*step/4;a->y=oy+a->vy*step/4;x=(a->x+128)>>8;y=(a->y+128)>>8;
   if(x==a->last_x&&y==a->last_y)continue;
   if(!game_walkable(g,x,y)||(x!=a->last_x&&y!=a->last_y&&(!game_walkable(g,x,a->last_y)||!game_walkable(g,a->last_x,y)))){
    if(s==SPELL_FIREBALL||s==SPELL_ELEMENTAL){effect_owner_enter(g,m->owner);if(s==SPELL_FIREBALL)fireball_blast(g,a->last_x,a->last_y,m->power);else blast(g,a->last_x,a->last_y,m->power,s,1);persistent_add(SPELL_FIREBALL,0,0,a->last_x,a->last_y,15);dirty=1;}bolt_remove(a);break;}
   a->last_x=x;a->last_y=y;
   if(dirty){for(unsigned bucket=0;bucket<64;bucket++)heads[bucket]=255;
    for(int j=MAX_ENEMIES-1;j>=0;j--)if(g->enemies[j].alive==1){unsigned bucket=(g->enemies[j].x*3u+g->enemies[j].y)&63;next[j]=heads[bucket];heads[bucket]=(uint8_t)j;}dirty=0;}
   for(unsigned j=heads[(x*3u+y)&63];j!=255;j=next[j])if(g->enemies[j].alive==1&&g->enemies[j].x==x&&g->enemies[j].y==y&&game_monster_missile_target(g,j)){
    if(s==SPELL_HOLY&&!monster_undead[g->enemies[j].kind])continue;
    effect_owner_enter(g,m->owner);
    int chance=game_effective_stat(1)-2*(int)game_monster_level(g->enemies[j].kind)+50+(game_hero_class()==2?20:0);if(chance<5)chance=5;if(chance>95)chance=95;
    if(!game_spell_stoned(j)&&random_n(100)>=(unsigned)chance)break;
    unsigned power=s==SPELL_BONE_SPIRIT?(unsigned)g->enemies[j].hp/3:m->power;
    if(s==SPELL_FIREBALL){hit(g,j,power,s);fireball_blast(g,x,y,power);persistent_add(SPELL_FIREBALL,0,0,x,y,15);}
    else if(s==SPELL_ELEMENTAL){blast(g,x,y,power,s,1);persistent_add(SPELL_FIREBALL,0,0,x,y,15);}else hit(g,j,power,s);dirty=1;
    if(s!=SPELL_LIGHTNING&&s!=SPELL_CHAIN&&s!=SPELL_WAVE)bolt_remove(a);break;
   }
  }
 }
 effect_owner_leave(g,previous);
}

void game_spells_reset(void){game_item_drag_reset();effects_reset();effect_mask=0;for(unsigned i=0;i<4;i++){spell_pending[i]=0;shield[i]=0;cast_source[i]=source_kind[i]=0;source_seed[i]=0;cast_level[i]=cast_cost[i]=0;}for(unsigned i=0;i<MAX_SPELL_CASTS;i++)game_spell_casts[i]=(SpellCast){0};for(unsigned i=0;i<MAX_SPELL_BOLTS;i++)game_spell_bolts[i]=(SpellBolt){0};spell_rng=0x86123456;}
void game_spells_remove_owner(unsigned owner){
 if(owner>=4)return;
 spell_pending[owner]=shield[owner]=cast_source[owner]=source_kind[owner]=cast_level[owner]=0;
 source_seed[owner]=cast_cost[owner]=0;effect_mask&=~(1u<<owner);game_spell_portal_consume(owner);
 for(unsigned i=0;i<MAX_SPELL_BOLTS;i++)if(game_spell_bolts[i].cast&&game_spell_casts[game_spell_bolts[i].cast-1].owner==owner)bolt_remove(&game_spell_bolts[i]);
 for(unsigned i=0;i<MAX_SPELL_CASTS;i++)if(game_spell_casts[i].owner==owner)game_spell_casts[i]=(SpellCast){0};
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].owner==owner){persistent[i]=(PersistentSpell){0};golem_stuck[i]=0;}
 for(unsigned i=0;i<NOVA_SLOTS;i++)if(nova_waves[i].owner==owner)nova_waves[i]=(NovaWave){0};
}
uint32_t game_spells_hash(uint32_t h){
 h=effects_hash(h);
 const uint8_t *p=(const uint8_t*)game_spell_casts;for(unsigned i=0;i<sizeof(game_spell_casts);i++)h=(h^p[i])*16777619u;
 p=(const uint8_t*)game_spell_bolts;for(unsigned i=0;i<sizeof(game_spell_bolts);i++)h=(h^p[i])*16777619u;
 for(unsigned i=0;i<4;i++){h=(h^spell_pending[i])*16777619u;h=(h^shield[i])*16777619u;h=(h^cast_source[i])*16777619u;h=(h^source_seed[i])*16777619u;h=(h^source_kind[i])*16777619u;h=(h^cast_level[i])*16777619u;h=(h^cast_cost[i])*16777619u;}
 for(unsigned p=0;p<4;p++){h=(h^portal[p].floor)*16777619u;h=(h^portal[p].x)*16777619u;h=(h^portal[p].y)*16777619u;}return (h^spell_rng)*16777619u;
}
void game_spell_select_slot(unsigned slot){if(slot<4){hero_progress.spell_active_slot=slot;hero_progress.spell_selected=hero_progress.spell_slot[slot];}}
int game_spell_bind_slot(unsigned slot,unsigned s){if(slot>=4||(s&&!game_spell_available(s)))return 0;if(s)for(unsigned i=0;i<4;i++)if(hero_progress.spell_slot[i]==s)hero_progress.spell_slot[i]=0;hero_progress.spell_slot[slot]=s;game_spell_select_slot(slot);return 1;}
int game_spell_request_selected(Game *g){unsigned slot=hero_progress.spell_active_slot;return game_spell_request(g,slot<4?hero_progress.spell_slot[slot]:0);}
/*8 payload bits excluding host clock2/3 and tag9/10. 0..151 bind,
 *152..155 select,156..192 cast (including the equipped-staff source). */
unsigned game_spell_wire(unsigned op,unsigned slot,unsigned s){
 if(slot>=4||!game_spell_choice_valid(s)||op>2)return 0;
 unsigned code=op==1?4*SPELL_CHOICES+slot:op==2?slot*SPELL_CHOICES+s:4*SPELL_CHOICES+3+s;
 if(!op&&!s)return 0;
 return SPELL_COMMAND|(code&3)|((code&124)<<2)|((code&128)<<4);
}
void game_spell_command(Game *g,unsigned command){
 if((command&SPELL_COMMAND)!=SPELL_COMMAND)return;unsigned code=(command&3)|((command>>2)&124)|((command>>4)&128);
 if(code<4*SPELL_CHOICES)game_spell_bind_slot(code/SPELL_CHOICES,code%SPELL_CHOICES);else if(code<4*SPELL_CHOICES+4)game_spell_select_slot(code-4*SPELL_CHOICES);else if(code<5*SPELL_CHOICES+3)game_spell_request(g,code-4*SPELL_CHOICES-3);
}
