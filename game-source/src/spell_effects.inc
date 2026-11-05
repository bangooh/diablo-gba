/* Persistent effects and radial waves do not consume the eight moving slots.
 * Coordinates are map cells; all state is deterministic and transient. */
enum { PERSISTENT_SLOTS=16, NOVA_SLOTS=4, NOVA_RADIUS=8, APOCALYPSE_VIEWS=4, APOCALYPSE_BATCH=4, APOCALYPSE_FIRST=PERSISTENT_SLOTS+NOVA_SLOTS*16 };
typedef struct {
 union {uint16_t ticks;int16_t hp;};uint16_t power;
 uint8_t x,y,owner,spell,level,delay;int8_t target;uint8_t reserved;
} PersistentSpell;
typedef struct {
 uint16_t power;uint8_t x,y,owner,age;
 uint8_t hit[(MAX_ENEMIES+7)/8];
} NovaWave;
_Static_assert(SPELL_EFFECT_VIEWS==APOCALYPSE_FIRST+APOCALYPSE_VIEWS,"effect view bounds");
_Static_assert(sizeof(PersistentSpell)==12,"compact persistent state");
static PersistentSpell persistent[PERSISTENT_SLOTS];
static uint8_t golem_stuck[PERSISTENT_SLOTS];
static NovaWave nova_waves[NOVA_SLOTS];
/* Presentation-only map bitmap: every affected cell survives monster death,
 * movement, full persistent slots and repeated casts. Duplicate waiting cells
 * coalesce; damage is never queued. Fits in IWRAM freed by relocating the main control shell to ROM. */
static struct {
 uint8_t pending[(MAP_W*MAP_H+7)/8];
 uint16_t active[APOCALYPSE_VIEWS],count,cursor;
 uint8_t ticks;
 uint32_t ready[(((MAP_W*MAP_H+7)/8)+31)/32];
} apocalypse __attribute__((section(".iwram.coop")));
#define EFFECT_FN __attribute__((section(".text.ui"),noinline))
static EFFECT_FN void apocalypse_queue(int x,int y){
 unsigned cell=(unsigned)y*MAP_W+x,bit=1u<<(cell&7);
 if(!(apocalypse.pending[cell/8]&bit)){apocalypse.pending[cell/8]|=bit;apocalypse.count++;apocalypse.ready[cell/256]|=1u<<((cell/8)&31);}
 effect_mask|=64;
}
static EFFECT_FN void apocalypse_step(void){
 if(apocalypse.ticks&&--apocalypse.ticks)return;
 for(unsigned i=0;i<APOCALYPSE_VIEWS;i++)apocalypse.active[i]=0;
 unsigned n=0;
 while(apocalypse.count&&n<APOCALYPSE_BATCH){
  unsigned word=apocalypse.cursor,bits=apocalypse.ready[word];
  if(!bits){if(++apocalypse.cursor==sizeof apocalypse.ready/sizeof apocalypse.ready[0])apocalypse.cursor=0;continue;}
  unsigned byte=word*32+(unsigned)__builtin_ctz(bits),bit=(unsigned)__builtin_ctz((unsigned)apocalypse.pending[byte]);
  apocalypse.pending[byte]&=(uint8_t)~(1u<<bit);apocalypse.count--;
  if(!apocalypse.pending[byte])apocalypse.ready[word]&=~(1u<<(byte&31));
  apocalypse.active[n++]=byte*8+bit+1;
 }
 apocalypse.ticks=n?15:0;
}
static EFFECT_FN int persistent_add(unsigned spell,unsigned level,unsigned power,int x,int y,unsigned ticks){
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(!persistent[i].spell){
  if(spell==SPELL_STONE)effect_mask|=1;if(spell==SPELL_GOLEM)effect_mask|=2;if(spell==SPELL_FIREWALL)effect_mask|=4;if(spell==SPELL_GUARDIAN)effect_mask|=16;if(spell==SPELL_FLASH||spell==SPELL_APOCALYPSE||spell==SPELL_RESURRECT||spell==SPELL_FIREBALL)effect_mask|=32;
  golem_stuck[i]=0;
  persistent[i]=(PersistentSpell){.ticks=ticks,.power=power,.x=x,.y=y,.owner=owner_id(),.spell=spell,.level=level,.target=-1};return i+1;
 }
 if(spell!=SPELL_FLASH&&spell!=SPELL_APOCALYPSE)for(unsigned i=0;i<PERSISTENT_SLOTS;i++)if(persistent[i].spell==SPELL_FLASH||persistent[i].spell==SPELL_APOCALYPSE){persistent[i]=(PersistentSpell){0};return persistent_add(spell,level,power,x,y,ticks);}
 return 0;
}
static EFFECT_FN int nova_add(Game *g,unsigned power){
 for(unsigned i=0;i<NOVA_SLOTS;i++)if(!nova_waves[i].age){
  effect_mask|=8;
  nova_waves[i]=(NovaWave){.power=power,.x=g->x,.y=g->y,.owner=owner_id(),.age=1};return 1;
 }
 return 0;
}
/* Only switch the expensive character context when a spell actually acts. */
static unsigned effect_owner_enter(Game *g,unsigned owner){unsigned previous=coop_turn;if(coop_active&&owner!=previous){coop_capture(g,previous);coop_select(g,owner);}return previous;}
static void effect_owner_leave(Game *g,unsigned previous){if(coop_active&&coop_turn!=previous){coop_capture(g,coop_turn);coop_select(g,previous);}}
/* Fixed direction order is part of deterministic multiplayer simulation. */
static EFFECT_FN __attribute__((minsize)) int golem_free(Game *g,int x,int y){
 if(!free_tile(g,x,y)||(!coop_active&&x==g->x&&y==g->y))return 0;
 for(unsigned n=0;n<PERSISTENT_SLOTS;n++)if(persistent[n].spell==SPELL_GOLEM&&persistent[n].x==x&&persistent[n].y==y)return 0;
 return 1;
}
static EFFECT_FN __attribute__((minsize)) int golem_recall(Game *g,PersistentSpell *e,int tx,int ty){
 for(int radius=1;radius<=2;radius++){for(int dy=-radius;dy<=radius;dy++){for(int dx=-radius;dx<=radius;dx++){
  if(abs_i(dx)!=radius&&abs_i(dy)!=radius)continue;
  int x=tx+dx,y=ty+dy;
  if(golem_free(g,x,y)&&line_clear(g,tx,ty,x,y)){
   e->x=x;e->y=y;e->delay=0;e->reserved&=7;e->target=-1;return 1;
  }
 }}}
 return 0;
}
static EFFECT_FN __attribute__((minsize)) int golem_move(Game *g,PersistentSpell *e,int tx,int ty,unsigned direction){
 int chosen=-1,best=100000;
 for(unsigned n=0;n<3;n++){
  unsigned d=n==0?direction:n==1?(direction+7)&7:(direction+1)&7;
  int x=e->x+dx8[d],y=e->y+dy8[d];
  if(!golem_free(g,x,y)||!line_clear(g,e->x,e->y,x,y))continue;
  int dx=tx-x,dy=ty-y,score=dx*dx+dy*dy;
  if(!n){chosen=d;break;}
  if(score<best){best=score;chosen=d;}
 }
 if(chosen<0)return 0;
 e->x+=dx8[chosen];e->y+=dy8[chosen];e->reserved=8|chosen;return 1;
}
static EFFECT_FN __attribute__((minsize)) void persistent_step(Game *g){
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++){
  PersistentSpell *e=&persistent[i];unsigned s=e->spell;if(s!=SPELL_GOLEM)golem_stuck[i]=0;if(!s)continue;
  if(s!=SPELL_GOLEM&&(!e->ticks||!--e->ticks)){*e=(PersistentSpell){0};continue;}
  int x=e->x,y=e->y;
  if(s==SPELL_STONE){int t=e->target;if(t<0||g->enemies[t].alive!=1||g->enemies[t].x!=x||g->enemies[t].y!=y)*e=(PersistentSpell){0};continue;}
  if(s==SPELL_FIREWALL){if(e->ticks%4)continue;unsigned previous=effect_owner_enter(g,e->owner);blast(g,x,y,e->power,s,0);effect_owner_leave(g,previous);continue;}
  if(s==SPELL_GUARDIAN){if(e->delay)e->delay--;if(e->reserved<15){e->reserved++;continue;}if(e->ticks<15||e->ticks%16)continue;int t=nearest(g,x,y,6);if(t<0||free_slot()<0)continue;
   unsigned previous=effect_owner_enter(g,e->owner);add_missile(g,SPELL_FIREBOLT,e->level,e->power,x,y,g->enemies[t].x,g->enemies[t].y,80,0);effect_owner_leave(g,previous);e->delay=3;continue;}
  if(s==SPELL_GOLEM){if(golem_stuck[i]&&golem_stuck[i]<60)golem_stuck[i]++;if(e->delay){
    if((e->reserved&24)==16&&e->delay==5&&e->target>=0){int t=e->target;
     if(g->enemies[t].alive==1&&abs_i(g->enemies[t].x-x)<=1&&abs_i(g->enemies[t].y-y)<=1&&line_clear(g,x,y,g->enemies[t].x,g->enemies[t].y)){unsigned previous=effect_owner_enter(g,e->owner);hit(g,t,e->power+random_n(9),s);effect_owner_leave(g,previous);}}
    e->delay--;continue;}e->reserved&=7;int t=nearest(g,x,y,8);
   int tx,ty;
   if(t>=0){golem_stuck[i]=0;tx=g->enemies[t].x;ty=g->enemies[t].y;}
   else{
    /* Follow the summoner, not the currently selected co-op context.
     * Keep two tiles of clearance and preserve the existing eight-tick gait. */
    e->target=-1;
    if(coop_active){
     if(e->owner>=4||!(coop_members&(1u<<e->owner))||coop_players[e->owner].hero.hp<=0){golem_stuck[i]=0;continue;}
     tx=coop_players[e->owner].x;ty=coop_players[e->owner].y;
    }else{if(g->hp<=0){golem_stuck[i]=0;continue;}tx=g->x;ty=g->y;}
    if(abs_i(tx-x)<8&&abs_i(ty-y)<8)golem_stuck[i]=0;
    if(abs_i(tx-x)<=2&&abs_i(ty-y)<=2)continue;
    if(golem_stuck[i]>=60){
     if(golem_recall(g,e,tx,ty)){golem_stuck[i]=0;continue;}
     e->delay=8;continue;
    }
   }
   int dx=tx-x,dy=ty-y;
   int sx=sign(dx),sy=sign(dy);unsigned direction=sy>0?(sx>0?0:sx<0?2:1):sy<0?(sx>0?6:sx<0?4:5):sx<0?3:7;e->reserved=direction;
   if(t>=0&&abs_i(dx)<=1&&abs_i(dy)<=1){e->target=t;e->delay=12;e->reserved|=16;}
   else{
    if(golem_move(g,e,tx,ty,direction))golem_stuck[i]=0;
    else if(t<0&&(abs_i(dx)>=8||abs_i(dy)>=8)&&!golem_stuck[i])golem_stuck[i]=1;
    e->delay=8;
   }
  }
 }
}
static EFFECT_FN void nova_step(Game *g){
 for(unsigned n=0;n<NOVA_SLOTS;n++){
  NovaWave *w=&nova_waves[n];if(!w->age)continue;
  if(w->age>NOVA_RADIUS){*w=(NovaWave){0};continue;}
  unsigned radius=w->age;int previous_radius=(int)radius-1;unsigned previous=coop_turn,entered=0;
  for(unsigned i=0;i<MAX_ENEMIES;i++){
   Enemy *e=&g->enemies[i];if(e->alive!=1||(w->hit[i/8]&(1u<<(i&7))))continue;
   int dx=e->x-w->x,dy=e->y-w->y,d=dx*dx+dy*dy;
   if(d>(int)(radius*radius)||(radius>1&&d<=previous_radius*previous_radius))continue;
   /* Mark a crossing once, including a failed hit roll or occluded target. */
   w->hit[i/8]|=(uint8_t)(1u<<(i&7));
   if(!game_monster_missile_target(g,i)||!line_clear(g,w->x,w->y,e->x,e->y))continue;
   if(!entered){previous=effect_owner_enter(g,w->owner);entered=1;}
   int chance=game_effective_stat(1)-2*(int)game_monster_level(e->kind)+50+(game_hero_class()==2?20:0);if(chance<5)chance=5;if(chance>95)chance=95;
   if(game_spell_stoned(i)||random_n(100)<(unsigned)chance)hit(g,i,w->power,SPELL_NOVA);
  }
  if(entered)effect_owner_leave(g,previous);w->age++;
 }
}
/* World-pixel views are presentation only; one shared image draws all sixteen
 * wave samples. Physics never depends on camera, OAM admission or lighting. */
unsigned game_spell_effects_active(void){return effect_mask&126;}
EFFECT_FN unsigned game_spell_effect_view_phase(unsigned index,unsigned phase,int *wx,int *wy){
 if(index>=APOCALYPSE_FIRST){unsigned n=index-APOCALYPSE_FIRST;
  if(n>=APOCALYPSE_VIEWS||!apocalypse.ticks||!apocalypse.active[n])return 0;
  unsigned cell=apocalypse.active[n]-1;int x=cell%MAP_W,y=cell/MAP_W;
  *wx=DUNGEON_ORIGIN+(x-y)*16;*wy=72+(x+y)*8;return SPELL_APOCALYPSE;}
 if(index<PERSISTENT_SLOTS){const PersistentSpell *e=&persistent[index];if(!e->spell||e->spell==SPELL_STONE)return 0;
  *wx=DUNGEON_ORIGIN+((int)e->x-e->y)*16;*wy=72+(e->x+e->y)*8;
  if(e->spell==SPELL_GOLEM&&(e->reserved&24)==8){int d=e->reserved&7,remaining=e->delay*3-(int)phase;if(remaining<0)remaining=0;*wx-=(dx8[d]-dy8[d])*16*remaining/24;*wy-=(dx8[d]+dy8[d])*8*remaining/24;}
  return e->spell;}
 index-=PERSISTENT_SLOTS;if(index>=NOVA_SLOTS*16)return 0;
 const NovaWave *w=&nova_waves[index/16];if(!w->age)return 0;
 static const int16_t circle[16]={256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
 unsigned direction=index%16;int radius=(w->age-1)*256+(int)(phase<3?phase:0)*256/3;if(radius<64)radius=64;
 int dx=circle[direction]*radius/256,dy=circle[(direction+12)%16]*radius/256;

 *wx=DUNGEON_ORIGIN+((int)w->x-w->y)*16+(dx-dy)/16;
 *wy=72+(w->x+w->y)*8+(dx+dy)/32;return SPELL_NOVA;
}
EFFECT_FN unsigned game_spell_effect_view(unsigned index,int *wx,int *wy){return game_spell_effect_view_phase(index,0,wx,wy);}
EFFECT_FN unsigned game_spell_firewall_stage(unsigned index){
 if(index>=PERSISTENT_SLOTS||persistent[index].spell!=SPELL_FIREWALL)return 0;
 const PersistentSpell *e=&persistent[index];unsigned total=(10+e->level)*16,age=total>=e->ticks?total-e->ticks:0;
 if(e->ticks<=13)return e->ticks;return age<13?age+1:0;
}
EFFECT_FN unsigned game_spell_effect_pose(unsigned index,unsigned clock,unsigned *frame){
 if(index>=APOCALYPSE_FIRST&&index<APOCALYPSE_FIRST+APOCALYPSE_VIEWS){
  if(!apocalypse.ticks||!apocalypse.active[index-APOCALYPSE_FIRST])return 0;
  *frame=15-apocalypse.ticks;return 9;}
 if(index>=PERSISTENT_SLOTS)return 0;const PersistentSpell *e=&persistent[index];unsigned s=e->spell;
 if(s==SPELL_GOLEM){unsigned state=e->reserved&24;*frame=state==16?(12-e->delay):state==8?(8-e->delay)*2:0;if(*frame>(state==16?11u:15u))*frame=state==16?11:15;return (state==16?61:53)+(e->reserved&7);}
 if(s==SPELL_FLASH){*frame=19-e->ticks;return 13;}
 if(s==SPELL_APOCALYPSE){*frame=15-e->ticks;return 9;}
 if(s==SPELL_RESURRECT){*frame=16-e->ticks;return 47;}
 if(s==SPELL_FIREBALL){*frame=15-e->ticks;return 50;}
 if(s==SPELL_GUARDIAN){if(e->reserved<15){*frame=e->reserved;return 51;}if(e->ticks<15){*frame=e->ticks-1;return 51;}if(e->delay){*frame=3-e->delay;return 52;}*frame=(clock/3)%14;return 3;}
 return 0;
}
static void effects_reset(void){for(unsigned i=0;i<PERSISTENT_SLOTS;i++)golem_stuck[i]=0;uint8_t *a=(uint8_t*)&apocalypse;for(unsigned i=0;i<sizeof apocalypse;i++)a[i]=0;for(unsigned i=0;i<PERSISTENT_SLOTS;i++)persistent[i]=(PersistentSpell){0};for(unsigned i=0;i<NOVA_SLOTS;i++)nova_waves[i]=(NovaWave){0};}
static uint32_t effects_hash(uint32_t h){
 for(unsigned i=0;i<PERSISTENT_SLOTS;i++)h=(h^golem_stuck[i])*16777619u;
 /* Structures have explicit zeroed spare bytes and no pointer/presentation state. */
 const uint8_t *p=(const uint8_t*)persistent;for(unsigned i=0;i<sizeof persistent;i++)h=(h^p[i])*16777619u;
 p=(const uint8_t*)nova_waves;for(unsigned i=0;i<sizeof nova_waves;i++)h=(h^p[i])*16777619u;p=(const uint8_t*)&apocalypse;for(unsigned i=0;i<sizeof apocalypse;i++)h=(h^p[i])*16777619u;return h;
}
