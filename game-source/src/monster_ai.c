#include "game.h"
#include "performance_options.h"
#include "coop.h"
#include "world_rooms.h"
#include "../generated/combat_assets.h"
#include "../generated/monster_death.h"
/* Adapted from Devilution MAI_Zombie / MAI_SkelSd / MAI_Fallen / M_FallenFear.
 * Per-monster action clocks run at20Hz, independent of display frames. */
typedef uint8_t MonsterClock[MAX_ENEMIES] __attribute__((aligned(4)));
static MonsterClock walk,wait,delayed;
static MonsterClock seen,last_x,last_y;
static MonsterClock from_x,from_y;
static MonsterClock retreat,rally,special;
static MonsterClock idle,fraction,away;
static MonsterClock hurt,path_count;
static uint32_t rng;
#define AI_ROM __attribute__((section(".text.ui"),noinline))
#include "monster_pack.inc"
enum { SNEAK_VISIBLE=1,SNEAK_HIDDEN,SNEAK_FADE_IN,SNEAK_FADE_OUT,
       COUNSEL_FADE_IN=5,COUNSEL_FADE_OUT=6,COUNSEL_FADE_TICKS=20,
       SNEAK_FADE_TICKS=15,RHINO_CIRCLE=1,RHINO_CHARGE=2,CHARGE_STEP_TICKS=2 };
static int rhino(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_RHINO;}
static int sneak(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_SNEAK;}
static int counselor(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_COUNSLR;}
static int goat(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_GOATMC;}
AI_ROM int game_enemy_spawning(const Game *g,int i){
 unsigned kind=g->enemies[i].kind;
 return special[i]&&(kind==10||kind==13||(kind>=MONSTER_CORPSE_BOW&&kind<=MONSTER_BURNING_BOW));
}
AI_ROM int game_enemy_charging(const Game *g,int i){unsigned kind=g->enemies[i].kind;return (rhino(kind)||monster71_native[kind]==40||game_monster_snake(kind))&&retreat[i]==RHINO_CHARGE;}
AI_ROM int game_enemy_hidden(const Game *g,int i){
 return i>=0&&i<MAX_ENEMIES&&(sneak(g->enemies[i].kind)?rally[i]==0||rally[i]==SNEAK_HIDDEN:counselor(g->enemies[i].kind)&&rally[i]==SNEAK_HIDDEN)&&!game_spell_stoned(i);
}
AI_ROM int game_monster_stonable(const Game *g,int i){
 if(i<0||i>=MAX_ENEMIES||game_enemy_charging(g,i))return 0;
 if((sneak(g->enemies[i].kind)||counselor(g->enemies[i].kind))&&special[i])return 0;
 return !game_enemy_hidden(g,i);
}
AI_ROM unsigned game_enemy_opacity(const Game *g,int i){
 if(game_enemy_hidden(g,i))return 0;
 return 8; /* Native CL2 special frames supply the fade alpha. */
}
AI_ROM unsigned game_enemy_fade_frame(int i){
 if(rally[i]==COUNSEL_FADE_IN)return special[i]-1;
 if(rally[i]==COUNSEL_FADE_OUT)return COUNSEL_FADE_TICKS-special[i];
 return rally[i]==SNEAK_FADE_OUT?special[i]-1:SNEAK_FADE_TICKS-special[i];
}
/* Retail CheckMonsterHit: Counselors cannot be hurt while relocating.
 * Their collision body remains present; reappearance is vulnerable. */
AI_ROM int game_monster_damageable(const Game *g,int i){
 return i>=0&&i<MAX_ENEMIES&&(!counselor(g->enemies[i].kind)||!retreat[i]);
}
/* MonsterMHit/MonsterTrapHit exclude charging actors and a retreating
 * Illusion Weaver. Player melee retains its separate vulnerability rules. */
AI_ROM int game_monster_missile_target(const Game *g,int i){
 return game_monster_damageable(g,i)&&!game_enemy_charging(g,i)&&!(g->enemies[i].kind==28&&retreat[i]==1);
}
AI_ROM void game_monster_reveal(Game *g,int i){
 if(game_monster_ai(g->enemies[i].kind)==MONSTER_AI_GARG){rally[i]=1;special[i]=retreat[i]=0;}
 if(sneak(g->enemies[i].kind)||counselor(g->enemies[i].kind)){rally[i]=SNEAK_VISIBLE;special[i]=0;if(counselor(g->enemies[i].kind))retreat[i]=0;}
}
AI_ROM uint32_t game_monsters_hash(uint32_t h){
 const uint8_t *const state[]={walk,wait,delayed,seen,last_x,last_y,from_x,from_y,retreat,rally,special,idle,fraction,away,hurt,path_count};
 for(unsigned a=0;a<sizeof(state)/sizeof(state[0]);a++)for(unsigned i=0;i<MAX_ENEMIES;i++)h=(h^state[a][i])*16777619u;
 return game_pack_hash((h^rng)*16777619u);
}
/* One-tick local occupancy index: 0 empty, actor+1 unique, 255 shared.
 * Out-of-window/shared/stale entries use the exact scan. Reserve both ends of
 * a walk, and record every new destination before the next actor takes a turn.
 * The 1024-byte index is on the stack, not the nearly full EWRAM heap. */
enum { OCCUPANCY_SIDE=32 };
static uint8_t *occupied_hint;
static int16_t hint_x,hint_y;
/* One AI tick, terrain/props only: 0 unknown, 1 blocked, 2 walkable.
 * Actor occupancy and firewalls remain live queries. Opening a door clears
 * this cache immediately. Neither cache contents nor timing change RNG. */
enum { WALK_CACHE_BYTES=OCCUPANCY_SIDE*OCCUPANCY_SIDE/4 };
static uint8_t *walk_cache;
static void walk_cache_clear(void){if(walk_cache)for(unsigned n=0;n<WALK_CACHE_BYTES/sizeof(uint32_t);n++)((uint32_t*)walk_cache)[n]=0;}
static AI_ROM int monster_walkable(const Game *g,int x,int y){
 unsigned dx=(unsigned)(x-hint_x),dy=(unsigned)(y-hint_y);
 if(!walk_cache||dx>=OCCUPANCY_SIDE||dy>=OCCUPANCY_SIDE)return game_walkable(g,x,y);
 unsigned cell=dy*OCCUPANCY_SIDE+dx,shift=(cell&3)*2,state=(walk_cache[cell>>2]>>shift)&3;
 if(!state){state=game_walkable(g,x,y)?2:1;walk_cache[cell>>2]|=state<<shift;}
 return state==2;
}
static void occupancy_mark(int x,int y,int actor){
 unsigned dx=(unsigned)(x-hint_x),dy=(unsigned)(y-hint_y);
 if(occupied_hint&&dx<OCCUPANCY_SIDE&&dy<OCCUPANCY_SIDE){
  uint8_t *cell=&occupied_hint[dy*OCCUPANCY_SIDE+dx];
  if(!*cell)*cell=(uint8_t)(actor+1);else if(*cell!=actor+1)*cell=255;
 }
}
#ifdef MONSTER_SCAN_PROFILE
unsigned long monster_scan_candidates,monster_scan_skipped;
#endif
Corpse game_corpses[MAX_ENEMIES];
#if COOP_DEATH_SPRITES
unsigned game_animated_corpses;
#endif
static int diablo_death(const Corpse *c){
 return c->kind==MONSTER_DIABLO&&dungeon_level==16&&quest_progress.state[5]==3&&quest_progress.stage[5]<=140;
}
AI_ROM void game_finale_restore(Game *g){
 if(g->area||dungeon_level!=16||quest_progress.state[5]!=3||quest_progress.stage[5]>140)return;
 g->won=quest_progress.stage[5]>=140;
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const Enemy *e=&g->enemies[i];if(e->alive||e->kind!=MONSTER_DIABLO)continue;
  unsigned slot=0;
  for(unsigned n=0;n<MAX_ENEMIES;n++){
   const Corpse *c=&game_corpses[n];
   if(c->ticks&&c->kind==MONSTER_DIABLO&&c->x==e->x&&c->y==e->y){slot=n;break;}
   if(c->ticks<game_corpses[slot].ticks)slot=n;
  }
  /* Only presentation is rebuilt. Dead enemy coordinates/facing and Q5's
   * timer are already archived; replaying the kill would award loot/XP. */
#if COOP_DEATH_SPRITES
  if(game_corpses[slot].ticks<=CORPSE_TICKS)game_animated_corpses++;
#endif
  game_corpses[slot]=(Corpse){CORPSE_TICKS+1,(uint8_t)e->x,(uint8_t)e->y,MONSTER_DIABLO,(uint8_t)(g->enemy_facing[i]&7)};
  break;
 }
}
void game_corpse_add(const Game *g,int i){
 game_pack_forget((unsigned)i);
 unsigned slot=0;
 for(unsigned n=0;n<MAX_ENEMIES;n++){
  if(!game_corpses[n].ticks){slot=n;break;}
  if(game_corpses[n].ticks<game_corpses[slot].ticks)slot=n;
 }
 const Enemy *e=&g->enemies[i];unsigned kind=game_monster_corpse_kind(e->kind);
#if COOP_DEATH_SPRITES
 if(game_corpses[slot].ticks<=CORPSE_TICKS)game_animated_corpses++;
#endif
 game_corpses[slot]=(Corpse){(uint16_t)(CORPSE_TICKS+monster_death_frames[kind]*monster_death_ticks[kind]+1),(uint8_t)e->x,(uint8_t)e->y,kind,(uint8_t)(g->enemy_facing[i]&7)};
}
unsigned game_corpse_frame(const Corpse *c){
 unsigned kind=game_monster_corpse_kind(c->kind);if(kind>=MONSTER_CORPSE_TYPES)return 0;
 /* Native Diablo remains in MM_DEATH until140 ticks; the animation wraps
  * instead of taking the ordinary final-frame AddDead/delete branch. */
 if(diablo_death(c))return monster_death_base[kind]+(c->dir&7)*monster_death_frames[kind]+quest_progress.stage[5]%monster_death_frames[kind];
 if(c->ticks<=CORPSE_TICKS)return kind*8+(c->dir&7);
 unsigned count=monster_death_frames[kind],rate=monster_death_ticks[kind];
 int elapsed=(int)(count*rate)-(int)(c->ticks-CORPSE_TICKS);
 unsigned frame=elapsed>0?(unsigned)elapsed/rate:0;
 if(frame>=count)frame=count-1;
 return monster_death_base[kind]+(c->dir&7)*count+frame;
}
unsigned game_enemy_hit(int i){return hurt[i];}
static int fallen(const Enemy *e){return game_monster_ai(e->kind)==MONSTER_AI_FALLEN;}
static int fallen_spear(unsigned kind){return monster71_native[game_monster_base_kind(kind)]>=4&&monster71_native[game_monster_base_kind(kind)]<=7;}

/* Renderer-facing remaining frames, including held and reverse poses. */
unsigned game_enemy_special(const Game *g,int i){
 if(game_monster_ai(g->enemies[i].kind)==MONSTER_AI_GARG){
  unsigned frames=monster71_special_frames[g->enemies[i].kind];
  unsigned rate=game_monster_base_kind(g->enemies[i].kind)<94?2:1;
  if(!rally[i])return frames;
  if(rally[i]==2)return frames-(special[i]?(special[i]-1)/rate:0);
  return (special[i]+rate-1)/rate;
 }
 return special[i];
}
unsigned game_fallen_retreat(int i){return retreat[i];}
unsigned game_fallen_rally(int i){return rally[i];}
void game_monster_reset(int i){
 monster_credit[i]=0;
 walk[i]=wait[i]=delayed[i]=seen[i]=last_x[i]=last_y[i]=from_x[i]=from_y[i]=0;
 retreat[i]=rally[i]=special[i]=idle[i]=fraction[i]=away[i]=hurt[i]=path_count[i]=0;
}
/* Four dead consecutive slots have no actor decisions between their resets.
 * Word-sized clears preserve all private clocks; live and co-op slots retain
 * the original per-actor order. Every caller proves four-byte alignment. */
static void monster_reset_four(unsigned i){
 __builtin_memset(__builtin_assume_aligned(walk+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(wait+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(delayed+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(seen+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(last_x+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(last_y+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(from_x+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(from_y+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(retreat+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(rally+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(special+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(idle+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(fraction+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(away+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(hurt+i,4),0,4);
 __builtin_memset(__builtin_assume_aligned(path_count+i,4),0,4);
 for(unsigned n=0;n<4;n++)monster_credit[i+n]=0;
}
static const int8_t vx[8]={1,0,-1,-1,-1,0,1,1},vy[8]={1,1,1,0,-1,-1,-1,0};
static int abs_i(int n){return n<0?-n:n;}
static unsigned random_n(unsigned n){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng%n;}
static int facing(int dx,int dy){int x=(dx>0)-(dx<0),y=(dy>0)-(dy<0);for(int d=0;d<8;d++)if(vx[d]==x&&vy[d]==y)return d;return 0;}
#include "monster_gargoyle.inc"
void game_ai_reset(void){
    game_pack_clear();
    occupied_hint=0;
    rng=0x6d2b79f5u;
#if COOP_DEATH_SPRITES
    game_animated_corpses=0;
#endif
    for(int i=0;i<MAX_ENEMIES;i++){game_monster_reset(i);game_corpses[i]=(Corpse){0};}
}
unsigned game_enemy_walk_ticks(const Game *g,int i){return game_enemy_charging(g,i)?CHARGE_STEP_TICKS:monster_defs[g->enemies[i].kind<MONSTER_TYPES?g->enemies[i].kind:0].walk;}
unsigned game_enemy_walking(int i){return walk[i];}
int game_monster_blocks(const Game *g,int x,int y,int except){
    unsigned dx=(unsigned)(x-hint_x),dy=(unsigned)(y-hint_y);
    if(occupied_hint&&dx<OCCUPANCY_SIDE&&dy<OCCUPANCY_SIDE){
        unsigned owner=occupied_hint[dy*OCCUPANCY_SIDE+dx];
        if(owner!=255){
            int result=-1;
            if(!owner||(int)owner==except+1)result=0;
            else {
                unsigned i=owner-1;const Enemy *e=&g->enemies[i];
                if(e->alive==1&&((e->x==x&&e->y==y)||(walk[i]&&from_x[i]==x&&from_y[i]==y)))result=1;
            }
            if(result>=0){
#ifdef MONSTER_SCAN_PROFILE
                monster_scan_skipped++;
#endif
                return result;
            }
        }
    }
    for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){int i=(int)coop_enemy_index(g,n);
#ifdef MONSTER_SCAN_PROFILE
        monster_scan_candidates++;
#endif
        if(i!=except&&g->enemies[i].alive==1){
            if(g->enemies[i].x==x&&g->enemies[i].y==y)return 1;
            if(walk[i]&&from_x[i]==x&&from_y[i]==y)return 1;
        }
    }
    return 0;
}
/* Retail PosOkMonst/2/3 firewall rule. Resistance does not grant passage;
 * Diablo avoids even an immune firewall. Inferno is a separate missile. */
static int monster_hazard_free(const Game *g,int i,int x,int y){
 unsigned kind=g->enemies[i].kind;
 return ((game_monster_immunities(kind)&2)&&game_monster_base_kind(kind)!=MONSTER_DIABLO)||!game_spell_firewall_at(x,y);
}
static int monster_same_room(const Game *g,const Enemy *e){
 int room=game_room_at(e->x,e->y);return room>=0&&room==game_room_at(g->x,g->y);
}
static int body_free(const Game *g,int i,int x,int y){return monster_walkable(g,x,y)&&!coop_blocks(x,y,-1)&&!(g->x==x&&g->y==y)&&!game_monster_blocks(g,x,y,i);}
static int free_cell(const Game *g,int i,int x,int y){return body_free(g,i,x,y)&&monster_hazard_free(g,i,x,y);}
static int can_step(const Game *g,int i,int dir){
    const Enemy *e=&g->enemies[i];int dx=vx[dir],dy=vy[dir];
    return ((!game_monster_is_unique(e->kind)&&!game_monster_is_pack_minion(e->kind))||game_pack_step_allowed(g,i,e->x+dx,e->y+dy))&&free_cell(g,i,e->x+dx,e->y+dy)&&(!dx||!dy||(free_cell(g,i,e->x+dx,e->y)&&free_cell(g,i,e->x,e->y+dy)));
}
/* Deterministic one-cell displacement away from the hit source. Cancel a
 * reserved walk only after a legal destination is found; no wall tunnelling. */
void game_monster_knockback(Game *g,unsigned i,int fromx,int fromy){
 if(i>=MAX_ENEMIES||!game_monster_damageable(g,(int)i)||g->enemies[i].alive!=1||g->enemies[i].kind==MONSTER_DIABLO||game_finale_protected(g->enemies[i].kind)||game_spell_stoned(i))return;
 Enemy *e=&g->enemies[i];int dx=(e->x>fromx)-(e->x<fromx),dy=(e->y>fromy)-(e->y<fromy);
 if((!dx&&!dy)||!can_step(g,i,facing(dx,dy)))return;
 e->x+=dx;e->y+=dy;walk[i]=0;from_x[i]=e->x;from_y[i]=e->y;
 g->enemy_attack[i]=0;wait[i]=delayed[i]=special[i]=0;g->enemy_facing[i]=facing(-dx,-dy);occupancy_mark(e->x,e->y,i);
 if(rhino(e->kind)||game_monster_bat(e->kind)||game_monster_snake(e->kind))retreat[i]=rally[i]=0;
 if(game_monster_ai(e->kind)==MONSTER_AI_GARG)game_monster_reveal(g,i);
 if(sneak(e->kind)){game_monster_reveal(g,i);retreat[i]=1;fraction[i]=0;}
}
/* Retail DoVision tests nBlockTable[dPiece], not object._oSolidFlag.
 * Furniture still blocks movement, but must not put an enemy behind a barrel
 * or sarcophagus to sleep. Closed doors change the native terrain piece, so
 * retain them as opaque. The port keeps its bounded terrain/radius model.
 * Reuse the walk cache's clear-cell fast path; no second persistent map. */
static AI_ROM int monster_sight_cell(const Game *g,int x,int y){
    if(monster_walkable(g,x,y))return 1;
    unsigned dx=(unsigned)(x-hint_x),dy=(unsigned)(y-hint_y);
    unsigned cached=walk_cache&&dx<OCCUPANCY_SIDE&&dy<OCCUPANCY_SIDE;
    unsigned cell=dy*OCCUPANCY_SIDE+dx,shift=(cell&3)*2;
    /* State3: impassable furniture, already verified transparent to sight.
     * State2 stays walkable; doors/terrain remain opaque and door edits clear
     * the entire one-tick cache. No persistent RAM or stale cross-tick keys. */
    if(cached&&((walk_cache[cell>>2]>>shift)&3)==3)return 1;
    if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H||!g->map[y][x])return 0;
    for(unsigned p=0;p<MAX_PROPS;p++){
        const Prop *prop=&g->props[p];
        if(prop->x==x&&prop->y==y&&!prop->state&&game_prop_is_door(prop->kind))return 0;
    }
    if(cached)walk_cache[cell>>2]|=3u<<shift;
    return 1;
}
static int visible(const Game *g,const Enemy *e){
    int x=e->x,y=e->y,dx=abs_i(g->x-x),dy=abs_i(g->y-y),sx=x<g->x?1:-1,sy=y<g->y?1:-1,err=dx-dy;
    if(dx>8||dy>8)return 0;
    while(x!=g->x||y!=g->y){int nx=x,ny=y,v=2*err;if(v>-dy){err-=dy;nx+=sx;}if(v<dx){err+=dx;ny+=sy;}
        if(!monster_sight_cell(g,nx,ny))return 0;
        /* Vision can pass one open side of a corner (DoVision's OR).
         * can_step/hit_ok keep both sides clear for walking/melee. */
        if(nx!=x&&ny!=y&&!monster_sight_cell(g,nx,y)&&!monster_sight_cell(g,x,ny))return 0;
        x=nx;y=ny;
    }return 1;
}
static int hit_ok(const Game *g,const Enemy *e){
    int dx=g->x-e->x,dy=g->y-e->y;
    return abs_i(dx)<=1&&abs_i(dy)<=1&&(!dx||!dy||(monster_walkable(g,e->x+dx,e->y)&&monster_walkable(g,e->x,e->y+dy)));
}
/* M_StartHit: Sneaks always stagger; other families require level+3 HP.
 * Stone keeps its pose. A normal walk returns to its reserved origin. */
AI_ROM void game_monster_damage_reaction(Game *g,int i,unsigned damage){
 if(!game_spell_stoned(i)&&gargoyle_lift(g,i))return;
 if(sneak(g->enemies[i].kind)||damage>=game_monster_level(g->enemies[i].kind)+3)game_monster_hit(g,i);
}
void game_monster_hit(Game *g,int i){
 if(game_spell_stoned(i)||!game_monster_damageable(g,i))return;
 if(gargoyle_lift(g,i))return;
 if(counselor(g->enemies[i].kind))game_monster_reveal(g,i);
 if(game_enemy_charging(g,i)){retreat[i]=rally[i]=walk[i]=0;from_x[i]=g->enemies[i].x;from_y[i]=g->enemies[i].y;}
 if(sneak(g->enemies[i].kind)){game_monster_reveal(g,i);retreat[i]=1;fraction[i]=0;seen[i]=100;last_x[i]=g->x;last_y[i]=g->y;}
 if(walk[i]){g->enemies[i].x=from_x[i];g->enemies[i].y=from_y[i];walk[i]=0;occupancy_mark(from_x[i],from_y[i],i);}
 hurt[i]=monster_defs[g->enemies[i].kind].hit_frames+1;g->enemy_attack[i]=0;special[i]=0;wait[i]=delayed[i]=0;
 if(game_monster_ai(g->enemies[i].kind)==MONSTER_AI_GARG){rally[i]=GARG_AWAKE;retreat[i]=0;}
 if(monster71_native[g->enemies[i].kind]==39){
  Enemy *e=&g->enemies[i];int rx=random_n(2)?1:-1,ry=random_n(2)?1:-1,done=0;
  for(int y=-1;y<=1&&!done;y++)for(int x=-1;x<1&&!done;x++)if(x||y){int nx=g->x+rx*y,ny=g->y+ry*x;
   if(nx!=e->x&&ny!=e->y&&free_cell(g,i,nx,ny)&&!coop_blocks(nx,ny,-1)&&!(nx==g->x&&ny==g->y)){e->x=from_x[i]=nx;e->y=from_y[i]=ny;occupancy_mark(nx,ny,i);done=1;}
  }
 }
 if(!walk[i])g->enemy_facing[i]=facing(g->x-g->enemies[i].x,g->y-g->enemies[i].y);
}
/* The TK caller has already validated and moved to its destination. Native
 * M_GetKnockback interrupts the pose without replacing a Counselor's goal or
 * hidden flag: pushing a fading mage must not remove relocation immunity.
 * Retain the port's away-from-caster destination rule and other-family reset. */
AI_ROM void game_monster_telekinesis(Game *g,int i){
 if(i<0||i>=MAX_ENEMIES)return;
 if(!counselor(g->enemies[i].kind)){game_monster_reset(i);game_monster_reveal(g,i);return;}
 walk[i]=wait[i]=delayed[i]=special[i]=path_count[i]=0;g->enemy_attack[i]=0;
 from_x[i]=g->enemies[i].x;from_y[i]=g->enemies[i].y;
 hurt[i]=monster_defs[g->enemies[i].kind].hit_frames+1;
 if(!retreat[i])game_monster_reveal(g,i);
 occupancy_mark(g->enemies[i].x,g->enemies[i].y,i);
}
static void start_walk(Game *g,int i,int dir){
    Enemy *e=&g->enemies[i];from_x[i]=e->x;from_y[i]=e->y;e->x+=vx[dir];e->y+=vy[dir];
    g->enemy_facing[i]=dir;walk[i]=game_enemy_walk_ticks(g,i);occupancy_mark(e->x,e->y,i);
}
static void call_walk(Game *g,int i,int dir,int dumb){
    if(can_step(g,i,dir)){start_walk(g,i,dir);return;}if(dumb)return;
    int side=random_n(2)?1:-1,turn=random_n(2)?2:-2;
    const int candidates[4]={(dir+side+8)&7,(dir-side+8)&7,(dir+turn+8)&7,(dir-turn+8)&7};
    for(int n=0;n<4;n++)if(can_step(g,i,candidates[n])){start_walk(g,i,candidates[n]);return;}
}
#include "monster_path.inc"

/* M_FallenFear: base red spear/sword flee seven standing decisions.
 * Remember a separate heading so an in-flight walk/attack is never rotated. */
void game_fallen_fear(Game *g,int x,int y){
 for(int i=0;i<MAX_ENEMIES;i++){
  Enemy *e=&g->enemies[i];
  if(e->alive&&e->hp>0&&fallen(e)&&abs_i(x-e->x)<5&&abs_i(y-e->y)<5){
   retreat[i]=7;rally[i]=0;away[i]=facing(e->x-x,e->y-y);wait[i]=delayed[i]=0;
  }
 }
}
static void fallen_shout(Game *g,int i){
 Enemy *e=&g->enemies[i];unsigned intelligence=monster_defs[e->kind].intelligence,radius=2*intelligence+4;special[i]=fallen_spear(e->kind)?13:15;wait[i]=delayed[i]=0;
 /* Original health uses 1/64 HP: intelligence0 heals two fixed-point units. */
 /* Retain this actor's initial roll and SP/MP provenance across region visits. */
 if(!e->max_hp&&e->hp>0)e->max_hp=(unsigned)e->hp;
 if(e->hp<e->max_hp){fraction[i]+=2*intelligence+2;if(fraction[i]>=64){fraction[i]-=64;e->hp++;}}else fraction[i]=0;
 for(int j=0;j<MAX_ENEMIES;j++)if(g->enemies[j].alive&&fallen(&g->enemies[j])&&(unsigned)abs_i(e->x-g->enemies[j].x)<=radius&&(unsigned)abs_i(e->y-g->enemies[j].y)<=radius){
  rally[j]=30*intelligence+105;retreat[j]=0;seen[j]=100;last_x[j]=g->x;last_y[j]=g->y;wait[j]=delayed[j]=0;
 }
}
void game_corpses_step(void){
#if COOP_DEATH_SPRITES
    unsigned animated=0;
#endif
    for(int i=0;i<MAX_ENEMIES;i++)if(game_corpses[i].ticks){
     if(diablo_death(&game_corpses[i]))game_corpses[i].ticks=CORPSE_TICKS+1;
     else --game_corpses[i].ticks;
#if COOP_DEATH_SPRITES
     animated+=game_corpses[i].ticks>CORPSE_TICKS;
#endif
    }
#if COOP_DEATH_SPRITES
    game_animated_corpses=animated;
#endif
}
/* M_RoundWalk: try the tangent, then turn twice toward the target.
 * If all three are blocked, reverse the circle and try the opposite tangent. */
static int round_walk(Game *g,int i,int dir){
 int turn=away[i]?1:-1,tangent=(dir-2*turn+8)&7;
 for(int n=0;n<3;n++){
  int candidate=(tangent+n*turn+8)&7;
  if(can_step(g,i,candidate)){start_walk(g,i,candidate);return 1;}
 }
 away[i]^=1;call_walk(g,i,(tangent+4)&7,0);return walk[i]!=0;
}
/* Kept as a named adapter for the existing King/native differential test. */
#define king_round_walk round_walk
static AI_ROM void ranged_think(Game *g,int i,int sight,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);g->enemy_facing[i]=dir;
 /* MAI_Succ / MAI_Ranged, shared by all requested bow variants. */
 if(sight){
  if(abs_i(dx)<4&&abs_i(dy)<4&&random_n(100)<10*(intelligence+7))call_walk(g,i,(dir+4)&7,0);
  if(!walk[i])g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
 }else call_walk(g,i,dir,0);
}
static AI_ROM void fat_think(Game *g,int i,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;unsigned roll=random_n(100);int after_walk=delayed[i];delayed[i]=0;
 g->enemy_facing[i]=facing(last_x[i]-e->x,last_y[i]-e->y);if(idle[i]<255)idle[i]++;
 if(abs_i(dx)>=2||abs_i(dy)>=2){
  if((idle[i]>20&&roll<4*intelligence+20)||(after_walk&&roll<4*intelligence+70)){call_walk(g,i,g->enemy_facing[i],0);delayed[i]=walk[i]!=0;}
 }else if(adjacent){
  if(roll<4*intelligence+15){g->enemy_attack[i]=game_monster_attack_ticks(e->kind);idle[i]=0;}
  else if(roll<4*intelligence+20){special[i]=10;idle[i]=0;}
 }
}
static AI_ROM void goat_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;unsigned roll=random_n(100),distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y),after_walk=delayed[i];delayed[i]=0;g->enemy_facing[i]=dir;if(idle[i]<255)idle[i]++;
 if(distance>=2&&sight&&monster_same_room(g,e)){
  if(retreat[i]||(distance>=4&&random_n(4)==0)){
   if(!retreat[i]){fraction[i]=0;away[i]=random_n(2);}retreat[i]=1;
   if(fraction[i]++>=2*distance&&can_step(g,i,dir))retreat[i]=0;
   else {if(round_walk(g,i,dir))delayed[i]=1;else wait[i]=10+random_n(10);return;}
  }
 }else retreat[i]=0;
 if(distance>=2){
  if((idle[i]>20&&roll<2*intelligence+28)||(after_walk&&roll<2*intelligence+78)){call_walk(g,i,dir,0);delayed[i]=walk[i]!=0;}
 }else if(adjacent&&roll<2*intelligence+23){
  /* Retail MAI_Round(TRUE). Native AFNum2 is zero for these clips:
   * preserve the non-damaging special pose rather than inventing a hit. */
  if(e->hp*2<e->max_hp&&random_n(2))special[i]=12;
  else g->enemy_attack[i]=game_monster_attack_ticks(e->kind);idle[i]=0;
 }
}
/* MAI_Rhino / MI_Rhino: a fixed integer ray, never a homing walk. The
 * original missile motion is quantized to one tile per two AI ticks on GBA.
 * Goal bytes are exclusive to this family: idle/delayed=abs(dx/dy),
 * fraction=128+ray error, away=heading, rally=bounded remaining ray length. */
static void rhino_next(int *x,int *y,int *err,unsigned dx,unsigned dy,unsigned dir){
 int twice=*err*2;
 if(twice>-(int)dy){*err-=(int)dy;*x+=vx[dir];}
 if(twice<(int)dx){*err+=(int)dx;*y+=vy[dir];}
}
static int rhino_corner(const Game *g,int i,int x,int y){
 const Enemy *e=&g->enemies[i];
 return x==e->x||y==e->y||(free_cell(g,i,x,e->y)&&free_cell(g,i,e->x,y)&&!game_spell_golem_at(x,e->y,0)&&!game_spell_golem_at(e->x,y,0));
}
static AI_ROM int rhino_clear(const Game *g,int i,int tx,int ty){
 const Enemy *e=&g->enemies[i];int x=e->x,y=e->y,dx=abs_i(tx-x),dy=abs_i(ty-y),err=dx-dy,dir=facing(tx-x,ty-y);
 for(unsigned n=0;n<16;n++){
  int ox=x,oy=y;rhino_next(&x,&y,&err,dx,dy,dir);
  if(!monster_walkable(g,x,y)||((x!=tx||y!=ty)&&!monster_hazard_free(g,i,x,y))||game_monster_blocks(g,x,y,i)||game_spell_golem_at(x,y,0))return 0;
  if(x!=ox&&y!=oy&&(!free_cell(g,i,x,oy)||!free_cell(g,i,ox,y)||game_spell_golem_at(x,oy,0)||game_spell_golem_at(ox,y,0)))return 0;
  if(x==tx&&y==ty)return 1;
  if(coop_blocks(x,y,-1)||(g->x==x&&g->y==y))return 0;
 }return 0;
}
static void rhino_stop(Game *g,int i){
 retreat[i]=rally[i]=walk[i]=delayed[i]=idle[i]=fraction[i]=special[i]=0;
 from_x[i]=g->enemies[i].x;from_y[i]=g->enemies[i].y;
 g->enemy_attack[i]=0;wait[i]=1;
 if(g->enemies[i].kind>=MONSTER_FIEND){delayed[i]=2;hurt[i]=monster_defs[g->enemies[i].kind].hit_frames+1;}
}
static int rhino_player(const Game *g,int x,int y){
 if(g->hp>0&&g->x==x&&g->y==y)return coop_active?(int)coop_turn:0;
 if(coop_active)for(unsigned p=0;p<4;p++)if(p!=coop_turn&&(coop_members&(1u<<p))&&coop_players[p].hero.hp>0&&coop_players[p].x==x&&coop_players[p].y==y)return (int)p;
 return -1;
}
static AI_ROM void rhino_step(Game *g,int i){
 Enemy *e=&g->enemies[i];int x=e->x,y=e->y,err=(int)fraction[i]-128;unsigned dir=away[i];
 if(!rally[i]--){rhino_stop(g,i);return;}
 rhino_next(&x,&y,&err,idle[i],delayed[i],dir);
 if(!monster_walkable(g,x,y)||!rhino_corner(g,i,x,y)||game_monster_blocks(g,x,y,i)){rhino_stop(g,i);return;}
 int player=rhino_player(g,x,y),golem=game_spell_golem_at(x,y,0);
 if(player<0&&!golem&&!monster_hazard_free(g,i,x,y)){rhino_stop(g,i);return;}
 if(player>=0||golem){
  /* MissToMonst excludes Gloom's impact. Snake's native damage2 is zero
   * and it has no forced knockback: do not borrow Rhino damage/knockback. */
  if(e->kind>=MONSTER_FIEND){rhino_stop(g,i);return;}
  /* Native mMinDamage2/mMaxDamage2. PlaceUniqueMonst overrides both
   * attacks with unique damage, while its followers retain base damage2. */
  unsigned low=game_monster_damage_bound(e->kind,1,0);
  unsigned high=game_monster_damage_bound(e->kind,1,1);
  unsigned power=low+random_n(high-low+1),previous=coop_turn;
  rhino_stop(g,i); /* Also makes reflected damage safe: exactly one impact. */
  if(player<0){game_spell_golem_at(x,y,power);return;}
  if(coop_active){coop_capture(g,previous);coop_select(g,(unsigned)player);}
  game_receive_melee(g,i,power,random_n(100));
  if(g->hp>0){
   if(!g->hit_flash){g->reaction_block=0;g->reaction_duration=game_recovery_duration(game_hero_class(),0);g->hit_flash=g->reaction_duration;}
   g->attack_wait=g->attack_duration=g->move_wait=0;g->swing_target=-1;
   if(g->attack_queued!=2)g->attack_queued=0;
   int nx=x+vx[dir],ny=y+vy[dir];
   if(body_free(g,i,nx,ny)&&!game_spell_golem_at(nx,ny,0)&&(!vx[dir]||!vy[dir]||(body_free(g,i,nx,y)&&body_free(g,i,x,ny)&&!game_spell_golem_at(nx,y,0)&&!game_spell_golem_at(x,ny,0)))){g->x=nx;g->y=ny;}
  }
  if(coop_active){coop_capture(g,(unsigned)player);coop_select(g,previous);}
  return;
 }
 from_x[i]=e->x;from_y[i]=e->y;e->x=x;e->y=y;fraction[i]=(uint8_t)(err+128);
 walk[i]=CHARGE_STEP_TICKS;g->enemy_facing[i]=dir;occupancy_mark(x,y,i);
}
static AI_ROM void rhino_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),roll=random_n(100);
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y),after_walk=delayed[i];delayed[i]=0;g->enemy_facing[i]=dir;
 if(distance>=2){
  if(retreat[i]==RHINO_CIRCLE||(distance>=5&&random_n(4)!=0)){
   if(!retreat[i]){fraction[i]=0;away[i]=random_n(2);}retreat[i]=RHINO_CIRCLE;
   if(fraction[i]++>=2*distance||!monster_same_room(g,e))retreat[i]=0;
   else {if(king_round_walk(g,i,dir))delayed[i]=1;else wait[i]=10+random_n(10);return;}
  }
 }else retreat[i]=0;
 if(sight&&distance>=5&&roll<2*intelligence+43&&rhino_clear(g,i,g->x,g->y)){
  retreat[i]=RHINO_CHARGE;idle[i]=abs_i(dx);delayed[i]=abs_i(dy);fraction[i]=128+(int)idle[i]-(int)delayed[i];
  away[i]=facing(dx,dy);rally[i]=MAP_W+MAP_H;special[i]=6;rhino_step(g,i);return;
 }
 if(distance>=2){unsigned v=random_n(100);if(v<2*intelligence+33||(after_walk&&v<2*intelligence+83)){call_walk(g,i,dir,0);delayed[i]=walk[i]!=0;}else wait[i]=10+random_n(10);}
 else if(adjacent&&roll<2*intelligence+28)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
}
/* MAI_Bat/MAI_Snake share the bounded charge ray, but keep native decisions.
 * No additional per-actor RAM: exclusive goal bytes are reused by each AI. */
static void native_charge(Game *g,int i,int dx,int dy){
 retreat[i]=RHINO_CHARGE;idle[i]=abs_i(dx);delayed[i]=abs_i(dy);fraction[i]=128+(int)idle[i]-(int)delayed[i];
 away[i]=facing(dx,dy);rally[i]=MAP_W+MAP_H;special[i]=monster_defs[g->enemies[i].kind].walk;rhino_step(g,i);
}
static AI_ROM void bat_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),roll=random_n(100),after_walk=delayed[i]==1;
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);delayed[i]=0;g->enemy_facing[i]=dir;
 if(retreat[i]>=3){unsigned phase=retreat[i];retreat[i]=phase==3?4:0;call_walk(g,i,(dir+(phase==3?4:random_n(2)?1:7))&7,0);delayed[i]=walk[i]!=0;return;}
 if(sight&&monster71_native[e->kind]==40&&distance>=5&&roll<4*intelligence+33&&rhino_clear(g,i,g->x,g->y)){native_charge(g,i,dx,dy);return;}
 if(distance>=2){
  if((idle[i]>20&&roll<intelligence+13)||(after_walk&&roll<intelligence+63)){call_walk(g,i,dir,0);delayed[i]=walk[i]!=0;}
  if(!walk[i]&&idle[i]<255)idle[i]++;
 }else if(adjacent&&roll<4*intelligence+8){g->enemy_attack[i]=game_monster_attack_ticks(e->kind);retreat[i]=3;idle[i]=0;if(monster71_native[e->kind]==41)game_monster_cast(g,i,SPELL_LIGHTNING);}
}
static AI_ROM void snake_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 static const int8_t pattern[6]={1,1,0,-1,-1,0};Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),previous=delayed[i];delayed[i]=0;
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);g->enemy_facing[i]=dir;
 if(distance>=2){
  if(sight&&distance<3&&previous!=2&&rhino_clear(g,i,g->x,g->y)){native_charge(g,i,dx,dy);return;}
  if(previous==1||random_n(100)>=35-2*intelligence){
   unsigned target=(dir+pattern[fraction[i]%6])&7,delta=(target-away[i])&7;fraction[i]=(fraction[i]+1)%6;
   if(delta)away[i]=delta==4?target:(away[i]+(delta<4?1:7))&7;
   if(can_step(g,i,away[i]))start_walk(g,i,away[i]);
   else call_walk(g,i,dir,0);
  }else{wait[i]=15-intelligence+random_n(10);delayed[i]=1;}
 }else if(adjacent){
  if(previous||random_n(100)<intelligence+20)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
  else{wait[i]=10-intelligence+random_n(10);delayed[i]=1;}
 }
}
/* MAI_Sneak: rally=visibility state, special=15-tick transition,
 * retreat/fraction=retreat goal/attempts, delayed=previous walk. */
static AI_ROM void sneak_think(Game *g,int i,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),dist=5-intelligence;
 int after_walk=delayed[i],dir=facing(last_x[i]-e->x,last_y[i]-e->y);delayed[i]=0;
 if(distance>=dist+3||fraction[i]>8){retreat[i]=0;fraction[i]=0;}
 if(retreat[i]){dir=(dir+4)&7;if(e->kind==MONSTER_UNSEEN)dir=(dir+(random_n(2)?1:7))&7;}
 g->enemy_facing[i]=dir;
 unsigned roll=random_n(100);if(idle[i]<255)idle[i]++;
 if(distance<dist&&game_enemy_hidden(g,i)){rally[i]=SNEAK_FADE_IN;special[i]=SNEAK_FADE_TICKS;idle[i]=0;return;}
 if(distance>=dist+1&&!game_enemy_hidden(g,i)){rally[i]=SNEAK_FADE_OUT;special[i]=SNEAK_FADE_TICKS;idle[i]=0;return;}
 if(retreat[i]||(distance>=2&&((idle[i]>20&&roll<4*intelligence+14)||(after_walk&&roll<4*intelligence+64)))){
  if(fraction[i]<255)fraction[i]++;call_walk(g,i,dir,0);delayed[i]=walk[i]!=0;if(walk[i])return;
 }
 if(!game_enemy_hidden(g,i)&&adjacent&&roll<4*intelligence+10){g->enemy_facing[i]=facing(dx,dy);g->enemy_attack[i]=game_monster_attack_ticks(e->kind);idle[i]=0;}
}
static __attribute__((section(".text.ui"),noinline)) void king_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];int after_walk=delayed[i];delayed[i]=0;
 if(!seen[i])return;
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);g->enemy_facing[i]=dir;
 unsigned roll=random_n(100),distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);
 /* King reuses Fallen-only storage: retreat=move goal, fraction=attempts,
  * away=circle side. Native room IDs now gate circling separately from the
  * port's bounded line-of-sight approximation of full visibility. */
 if(distance>=2&&sight&&monster_same_room(g,e)){
  if(retreat[i]||(distance>=3&&random_n(4)==0)){
   if(!retreat[i]){fraction[i]=0;away[i]=random_n(2);}
   retreat[i]=1;
   unsigned attempts=fraction[i];if(fraction[i]<255)++fraction[i];
   if(attempts>=2*distance&&can_step(g,i,dir))retreat[i]=0;
   else if(king_round_walk(g,i,dir))delayed[i]=1;
   else wait[i]=10+random_n(10);
  }
 }else retreat[i]=0;
 if(retreat[i])return;
 if(!coop_active&&sight&&((distance>=3&&roll<4*intelligence+35)||roll<6)){
  /* An unsuccessful summon consumes this standing decision too. */
  int slot=-1,blocked=!can_step(g,i,dir);
  for(unsigned j=0;j<MAX_ENEMIES;j++){
   const Enemy *other=&g->enemies[j];
   if(other->alive&&other->x==e->x+vx[dir]&&other->y==e->y+vy[dir])blocked=1;
   if(j<MAX_ENEMIES-1&&!other->alive&&slot<0)slot=j;
  }
  if(!blocked&&slot>=0){
   static const uint8_t kinds[5]={10,13,MONSTER_CORPSE_BOW,MONSTER_HORROR_BOW,MONSTER_BURNING_BOW};unsigned kind=kinds[random_n(5)];
   game_pack_forget((unsigned)slot);game_monster_reset(slot);g->enemy_attack[slot]=0;g->enemy_facing[slot]=dir;
   g->enemies[slot]=(Enemy){e->x+vx[dir],e->y+vy[dir],game_monster_hp(kind,random_n(65535)),2,kind,0};special[slot]=17;special[i]=18; /* First active tick exposes frame0 after dormant activation. */
  }
  return;
 }
 if(distance>=2){
  unsigned movement=random_n(100);
  if(movement<intelligence+25||(after_walk&&movement<intelligence+75)){
   call_walk(g,i,dir,0);delayed[i]=walk[i]!=0;
  }else wait[i]=10+random_n(10);
 }else if(adjacent&&roll<intelligence+20)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
}
/* Lazarus ignores M_StartDelay in retail; ordinary Counselors remember it
 * for the guaranteed close-range Flash on their next standing decision. */
static void counsel_delay(const Game *g,int i,unsigned ticks){
 if(g->enemies[i].kind!=MONSTER_LAZARUS){wait[i]=ticks+1;delayed[i]=2;}
}
/* RoundRanged/Counselor use existing goal bytes, with no extra actor RAM. */
static __attribute__((section(".text.ui"),noinline)) void finale_think(Game *g,int i,int sight,int adjacent,int dx,int dy){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),roll=random_n(100);
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);g->enemy_facing[i]=dir;
 if(e->kind==MONSTER_DIABLO){
  if(distance>=2&&sight&&monster_same_room(g,e)){
   if(retreat[i]||(distance>=3&&random_n(4)==0)){
    if(!retreat[i]){fraction[i]=0;away[i]=random_n(2);}retreat[i]=1;
    if(fraction[i]++>=2*distance&&can_step(g,i,dir))retreat[i]=0;
    else if(roll<20){special[i]=16;return;}
    else {if(!king_round_walk(g,i,dir))wait[i]=5+random_n(10);return;}
   }
  }else retreat[i]=0;
  if(sight&&((distance>=3&&roll<25)||roll<20)){special[i]=16;return;}
  if(distance>=2)call_walk(g,i,dir,0);
  else if(adjacent&&roll<90)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
  if(!walk[i]&&!g->enemy_attack[i]&&!special[i])wait[i]=5+random_n(10);return;
 }
 if(counselor(e->kind)){
  unsigned intelligence=monster_defs[e->kind].intelligence;
  int after_delay=delayed[i]==2;delayed[i]=0;
  if(retreat[i]){
   if(retreat[i]==1&&fraction[i]++<=3)call_walk(g,i,(dir+4)&7,0);
   else if(retreat[i]==2&&distance>=2&&sight&&monster_same_room(g,e)&&(fraction[i]++<2*distance||!can_step(g,i,dir)))round_walk(g,i,dir);
   else {retreat[i]=0;rally[i]=COUNSEL_FADE_IN;special[i]=COUNSEL_FADE_TICKS;}
   if(!walk[i]&&!special[i])counsel_delay(g,i,5+random_n(10));return;
  }
  if(distance>=2){
   if(sight&&roll<5*(intelligence+10))g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
   else if(random_n(100)<30){retreat[i]=2;fraction[i]=0;away[i]=random_n(2);rally[i]=COUNSEL_FADE_OUT;special[i]=COUNSEL_FADE_TICKS;}
   else counsel_delay(g,i,2*(5-intelligence)+random_n(10));
  }else if(e->hp<(int)(e->max_hp?e->max_hp:game_monster_hp_bound(e->kind,1))/2){retreat[i]=1;fraction[i]=0;rally[i]=COUNSEL_FADE_OUT;special[i]=COUNSEL_FADE_TICKS;}
  else if(after_delay||roll<2*intelligence+20){game_monster_cast(g,i,SPELL_FLASH);g->enemy_attack[i]=game_monster_attack_ticks(e->kind);delayed[i]=1;}
  else counsel_delay(g,i,2*(5-intelligence)+random_n(10));
  return;
 }
 /* MAI_Succ is also used by bow monsters, with their original projectile. */
 ranged_think(g,i,sight,dx,dy,monster_defs[e->kind].intelligence);
}
#include "monster_completion.inc"
void game_monsters_step(Game *g){
    uint32_t hint[OCCUPANCY_SIDE*OCCUPANCY_SIDE/sizeof(uint32_t)];
    uint32_t terrain[WALK_CACHE_BYTES/sizeof(uint32_t)];
    hint_x=g->x-OCCUPANCY_SIDE/2;hint_y=g->y-OCCUPANCY_SIDE/2;
#ifndef MONSTER_NO_WALK_CACHE
    walk_cache=(uint8_t*)terrain;walk_cache_clear();
#else
    (void)terrain;
#endif
#ifndef MONSTER_NO_OCCUPANCY_HINT
    for(unsigned i=0;i<sizeof(hint)/sizeof(hint[0]);i++)hint[i]=0;
    occupied_hint=(uint8_t*)hint;hint_x=g->x-OCCUPANCY_SIDE/2;hint_y=g->y-OCCUPANCY_SIDE/2;
    for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1){
        occupancy_mark(g->enemies[i].x,g->enemies[i].y,i);
        if(walk[i])occupancy_mark(from_x[i],from_y[i],i);
    }
#else
    (void)hint;
#endif
    game_corpses_step();
    for(int i=0;i<MAX_ENEMIES;i++){
        if(!(i&3)&&i+3<MAX_ENEMIES&&
           !(g->enemies[i].alive|g->enemies[i+1].alive|g->enemies[i+2].alive|g->enemies[i+3].alive)){
            monster_reset_four((unsigned)i);
            for(unsigned n=0;n<4;n++)g->enemy_attack[i+n]=0;
            i+=3;continue;
        }
        Enemy *e=&g->enemies[i];const MonsterDef *def=&monster_defs[e->kind];unsigned intelligence=def->intelligence;
        if(coop_active&&e->alive==1)coop_ai_target(g,i);
        if(e->alive!=1){if(!e->alive)game_monster_reset(i);else {if(retreat[i]==RHINO_CHARGE&&game_enemy_charging(g,i))rhino_stop(g,i);walk[i]=0;}g->enemy_attack[i]=0;continue;}
        if(game_finale_protected(e->kind))continue;
        if(!e->max_hp&&e->hp>0)e->max_hp=counselor(e->kind)?game_monster_hp_bound(e->kind,1):(unsigned)e->hp;
        /* ProcessMonsters refreshes awareness before action dispatch, even
         * during walk/attack recovery. UCHAR_MAX is 255 simulation ticks;
         * losing sight must not replace the remembered coordinates. */
        /* OPT150 comparison: single-player decisions at10Hz; co-op remains6/10Hz. Motion,
         * attack impact, hit recovery and special-action clocks remain20Hz.
         * Co-op uses the same perception cadence; attack/movement clocks remain20Hz. */
        int think_now=((!coop_active || (abs_i(g->x-e->x)<=1 && abs_i(g->y-e->y)<=1)) ? (((unsigned)g->ticks+(unsigned)i)%2u)==0u : (((unsigned)g->ticks*3u+(unsigned)i)%10u)<3u);
        int sight=think_now?visible(g,e):0;
        if(sight){last_x[i]=g->x;last_y[i]=g->y;seen[i]=255;}
        else if(seen[i])--seen[i];
        if(game_spell_stoned(i))continue;
        if(game_monster_ai(e->kind)==MONSTER_AI_GARG&&gargoyle_step(g,i))continue;
        if(hurt[i]){--hurt[i];continue;} /* Pause decisions during hit recovery. */
        if(game_enemy_charging(g,i)){special[i]=special[i]>1?special[i]-1:e->kind>=MONSTER_FIEND?def->walk:6;if(walk[i]&&--walk[i])continue;rhino_step(g,i);continue;}
        if(!special[i]&&!game_enemy_hidden(g,i)&&game_spell_golem_hit(g,i,0)){
            walk[i]=0;if(g->enemy_attack[i]){--g->enemy_attack[i];if(g->enemy_attack[i]==game_monster_attack_ticks(e->kind)-def->impact){unsigned low=game_monster_damage_bound(e->kind,0,0),high=game_monster_damage_bound(e->kind,0,1);game_spell_golem_hit(g,i,low+random_n(high-low+1));}}
            else g->enemy_attack[i]=game_monster_attack_ticks(e->kind);continue;
        }
        if(!sneak(e->kind)&&!counselor(e->kind)&&game_monster_ai(e->kind)!=MONSTER_AI_GARG&&rally[i])--rally[i];
        if(special[i]){--special[i];if(completion_ranged(e->kind)&&delayed[i]==3&&special[i]==monster71_special_frames[e->kind]-monster71_special_impact[e->kind]){game_monster_cast(g,i,completion_spell(e->kind));delayed[i]=0;}if((sneak(e->kind)||counselor(e->kind))&&!special[i])rally[i]=(rally[i]==SNEAK_FADE_OUT||rally[i]==COUNSEL_FADE_OUT)?SNEAK_HIDDEN:SNEAK_VISIBLE;if(e->kind==MONSTER_DIABLO&&special[i]==5)game_monster_cast(g,i,SPELL_APOCALYPSE);continue;}
        if(walk[i]){idle[i]=0;--walk[i];continue;} /* Finish the visual step before attack/turn. */
        if(g->enemy_attack[i]){
            idle[i]=0;--g->enemy_attack[i];
            if(counselor(e->kind)||game_monster_ai(e->kind)==MONSTER_AI_SUCC){
              if(g->enemy_attack[i]==game_monster_attack_ticks(e->kind)-def->impact&&!delayed[i])game_monster_cast(g,i,counselor(e->kind)?SPELL_FIREBALL:SPELL_BLOOD_STAR);
              if(!g->enemy_attack[i]){wait[i]=game_monster_ai(e->kind)==MONSTER_AI_SUCC?random_n(20):0;delayed[i]=0;}continue;
            }
            if(game_monster_archer(e->kind)){
              if(g->enemy_attack[i]==game_monster_attack_ticks(e->kind)-def->impact)game_monster_arrow(g,i);
              if(!g->enemy_attack[i])wait[i]=random_n(20);continue;
            }
            if(g->enemy_attack[i]==game_monster_attack_ticks(e->kind)-def->impact&&hit_ok(g,e)&&facing(g->x-e->x,g->y-e->y)==g->enemy_facing[i]){
                /* M_TryH2HHit: equipment AC lowers the enemy hit chance. */
                int chance=(int)game_monster_accuracy(e->kind,0)+2*((int)game_monster_level(e->kind)-hero_progress.level)+30-(int)game_armor();
                int minimum=dungeon_level>=14&&dungeon_level<=16?20+5*(dungeon_level-14):15;
                if(chance<minimum)chance=minimum;if(random_n(100)>=(unsigned)chance)continue;
                unsigned low=game_monster_damage_bound(e->kind,0,0),high=game_monster_damage_bound(e->kind,0,1);
                unsigned damage=low+random_n(high-low+1),block_roll=random_n(100);
                game_receive_melee(g,i,damage,block_roll);
            }continue;
        }
        if(!think_now){
            /* Keep pre-decision idle delays in20Hz units. Otherwise the
             * Goat/Fat wake-up threshold would stretch from1s to10s. */
            if(seen[i] && idle[i]<255 && (fallen(e) || (!wait[i] &&
               (goat(e->kind) || sneak(e->kind) || game_monster_ai(e->kind)==MONSTER_AI_FAT ||
                (game_monster_bat(e->kind) && (abs_i(g->x-e->x)>=2 || abs_i(g->y-e->y)>=2))))))++idle[i];
            if(wait[i])--wait[i];continue;
        }
        if(game_monster_is_unique(e->kind)||game_monster_is_pack_minion(e->kind))game_pack_unity(g,(unsigned)i);

        if(fallen(e)&&seen[i]){
            /* Stand animation: N has11/12 frames and rate3 in monsterdata. */
            if(++idle[i]>=(fallen_spear(e->kind)?33:36)){
                idle[i]=0;if(random_n(4)==0){fallen_shout(g,i);continue;}
            }
            if(retreat[i]){--retreat[i];call_walk(g,i,away[i],0);continue;}
            if(rally[i]){
                if(hit_ok(g,e)){g->enemy_facing[i]=facing(g->x-e->x,g->y-e->y);g->enemy_attack[i]=game_monster_attack_ticks(e->kind);}
                else call_walk(g,i,facing(last_x[i]-e->x,last_y[i]-e->y),0);
                continue;
            }
        }
        if(wait[i]){--wait[i];continue;}
        if(game_monster_ai(e->kind)==MONSTER_AI_GARG&&completion_gargoyle(g,i,sight,g->x-e->x,g->y-e->y,intelligence))continue;
        if(game_monster_ai(e->kind)==MONSTER_AI_SCAV&&completion_scavenger(g,i))continue;
        if(monster_path_think(g,i))continue;
        int dx=g->x-e->x,dy=g->y-e->y,adjacent=hit_ok(g,e);
        if(completion_ranged(e->kind)){completion_ranged_think(g,i,sight,adjacent,dx,dy,intelligence);}
        else if(game_monster_bat(e->kind)){bat_think(g,i,sight,adjacent,dx,dy,intelligence);}
        else if(game_monster_snake(e->kind)){snake_think(g,i,sight,adjacent,dx,dy,intelligence);}
        else if(rhino(e->kind)){rhino_think(g,i,sight,adjacent,dx,dy,intelligence);}
        else if(sneak(e->kind)){sneak_think(g,i,adjacent,dx,dy,intelligence);}
        else if(game_monster_archer(e->kind)||game_monster_ai(e->kind)==MONSTER_AI_SUCC){
            ranged_think(g,i,sight,dx,dy,intelligence);
        }else if(counselor(e->kind)||e->kind==MONSTER_DIABLO){finale_think(g,i,sight,adjacent,dx,dy);}
        else if(e->kind==MONSTER_KING){
            king_think(g,i,sight,adjacent,dx,dy,intelligence);
        }else if(goat(e->kind)){
            goat_think(g,i,sight,adjacent,dx,dy,intelligence);
        }else if(game_monster_ai(e->kind)==MONSTER_AI_FAT){
            fat_think(g,i,adjacent,dx,dy,intelligence);
        }else if(e->kind==MONSTER_BUTCHER){
            if(!seen[i])continue;
            g->enemy_facing[i]=facing(last_x[i]-e->x,last_y[i]-e->y);
            if(adjacent)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
            else call_walk(g,i,g->enemy_facing[i],0);
        }else if(game_monster_ai(e->kind)==MONSTER_AI_ZOMBIE){
            if(!sight||random_n(100)>=2*intelligence+10)continue; /* Base zombie intelligence0. */
            if(adjacent){g->enemy_facing[i]=facing(dx,dy);g->enemy_attack[i]=game_monster_attack_ticks(e->kind);}
            else if(abs_i(dx)>=(int)(2*intelligence+4)||abs_i(dy)>=(int)(2*intelligence+4)){int dir=g->enemy_facing[i];if(random_n(100)<2*intelligence+20)dir=random_n(8);call_walk(g,i,dir,1);}
            else call_walk(g,i,facing(dx,dy),0);
        }else{
            if(!seen[i])continue;
            int ready=delayed[i];delayed[i]=0;
            if(sight&&adjacent){
                g->enemy_facing[i]=facing(dx,dy);
                if(ready||random_n(100)<2*intelligence+20)g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
                else {wait[i]=2*(5-intelligence)+random_n(10);delayed[i]=1;}
            }else{
                dx=last_x[i]-e->x;dy=last_y[i]-e->y;
                if(!dx&&!dy)continue; /* Awareness expires on its native timer. */
                g->enemy_facing[i]=facing(dx,dy);
                if(ready||random_n(100)>=35-4*intelligence)call_walk(g,i,g->enemy_facing[i],0);
                else {wait[i]=15-2*intelligence+random_n(10);delayed[i]=1;}
            }
        }
    }
    occupied_hint=0;
    walk_cache=0;
}
