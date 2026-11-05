/* AddObjTraps, AddTrap and Obj_Trap from preserved Devilution objects.cpp.
 * The emitter stores its ORIGINAL target slot: ordinary props never move or
 * recycle slots, so this is the native saved-coordinate link without RAM.
 * GBA adapters: immutable floor RNG stream, 80x80 crop, 64 shared prop slots,
 * existing authored-room exclusions, and one emitter per occupied wall cell.
 * No nearest-container reconstruction and no live coordinate flag bits. */
#include "wall_traps.h"
#include "world.h"
#include "hazards.h"
#include "coop.h"
#include "../generated/wall_traps.h"
#include "../generated/wall_trap_flags.inc"

_Static_assert(MAX_PROPS==64,"wall trap target slot uses six bits");
_Static_assert(PROP_WALL_TRAP_FIRST==53&&PROP_WALL_TRAP_LAST==58,"wall trap archive kinds");
enum { WALL_TRAP_SPENT=64 };
static int wall_target_kind(unsigned kind){
 return game_prop_is_door(kind)||kind==PROP_SARC||kind==PROP_CHEST||kind==PROP_CHEST_MEDIUM||kind==PROP_CHEST_LARGE||
        kind==PROP_KING_LEVER||kind==PROP_BONE_LEVER||kind==PROP_DIABLO_LEVER;
}
static int wall_record(const Prop *p){return game_prop_is_wall_trap(p->kind)&&p->x<80&&p->y<80&&p->state<128;}
int game_wall_trap_linked(const Game *g,unsigned target){
 if(target>=MAX_PROPS)return 0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(wall_record(&g->props[i])&&(g->props[i].state&63)==target)return 1;
 return 0;
}
int game_wall_trap_armed(const Game *g,unsigned target){
 if(g->area||target>=MAX_PROPS||(g->trap_spent&((uint64_t)1<<target)))return 0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(wall_record(&g->props[i])&&g->props[i].state==target)return 1;
 return 0;
}
unsigned game_wall_trap_frame(unsigned kind){
 return PROP_WALL_TRAP_BASE+(game_prop_is_wall_trap(kind)?(kind-PROP_WALL_TRAP_FIRST)/3:0);
}
static unsigned wall_random(uint32_t *seed,unsigned count){
 *seed=*seed*0x015a4e35u+1;uint32_t n=*seed;if(n&0x80000000u)n=0u-n;
 return (n>>16)%count;
}
static int wall_piece_ok(const WorldArt *art,unsigned level,int x,int y){
 unsigned macro=dungeon_workspace[(y/2)*40+x/2];
 if(!macro||macro>art->macro_count)return 0;
 unsigned piece=art->macros[(macro-1)*4+(y&1)*2+(x&1)];
 if(level<=4)return piece<WALL_TRAP_L1_PIECES&&((native_wall_trap_flags_l1[piece/8]>>(piece&7))&1);
 return piece<WALL_TRAP_L2_PIECES&&((native_wall_trap_flags_l2[piece/8]>>(piece&7))&1);
}
void game_wall_traps_generate(Game *g,uint32_t floor_seed){
 /* Retail excludes Hell; all 560 native L3 SOL trap flags are zero. Fixed
  * quest maps do not run AddObjTraps, so ordinary floors 1..8 are complete. */
 if(g->area||dungeon_level<1||dungeon_level>8)return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(game_prop_is_wall_trap(g->props[i].kind))return;
 if(dungeon_level<=4&&!game_dungeon_read_macros(g,(uint8_t(*)[40])dungeon_workspace))return;
 const WorldArt *art=game_world_art(dungeon_level<=4?REGION_KING:dungeon_level);
 if(!art)return;
 uint32_t seed=floor_seed;int previous=-1;
 unsigned chance=dungeon_level>=7?25:dungeon_level>=5?20:dungeon_level>=2?15:10;
 /* Select existing positive object anchors in native y-then-x scan order.
  * New emitters lie earlier than their owner and are never scanned again. */
 for(unsigned scan=0;scan<MAX_PROPS;scan++){
  int target=-1,cell=6400;
  for(unsigned i=0;i<MAX_PROPS;i++){
   const Prop *p=&g->props[i];if(!p->kind||p->x>=80||p->y>=80)continue;
   int at=p->y*80+p->x;if(at>previous&&at<cell){target=(int)i;cell=at;}
  }
  if(target<0)break;previous=cell;
  const Prop *owner=&g->props[target];
  if(wall_random(&seed,100)>=chance||!wall_target_kind(owner->kind))continue;
  unsigned axis=wall_random(&seed,2);int x=owner->x,y=owner->y;
  do{if(axis)y--;else x--;}while(x>=0&&y>=0&&g->map[y][x]);
  if(x<0||y<0||(axis?(int)owner->y-y:(int)owner->x-x)<=1||
     !wall_piece_ok(art,dungeon_level,x,y)||game_wall_trap_protected(x,y))continue;
  int slot=-1,occupied=0;
  for(unsigned i=0;i<MAX_PROPS;i++){
   const Prop *p=&g->props[i];if(!p->kind){if(slot<0)slot=(int)i;}
   else if(p->x==x&&p->y==y)occupied=1;
  }
  if(slot<0)break;if(occupied)continue;
  unsigned missile=wall_random(&seed,dungeon_level/3+1);
  /* Cleared native _oVar3 defaults to MIS_ARROW when AddTrap rolls >=3. */
  if(missile>2)missile=0;
  g->props[slot]=(Prop){x,y,PROP_WALL_TRAP_FIRST+axis*3+missile,(unsigned)target};
 }
}
static void wall_aim(const Game *g,const Prop *owner,int *tx,int *ty){
 int best=-1;*tx=owner->x;*ty=owner->y;
 if(coop_active){
  for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&coop_players[i].hero.hp>0){
   int x=coop_players[i].x,y=coop_players[i].y,dx=x-owner->x,dy=y-owner->y;
   int cell=y*80+x;if(x>=0&&y>=0&&x<80&&y<80&&dx>=-1&&dx<=1&&dy>=-1&&dy<=1&&cell>best){best=cell;*tx=x;*ty=y;}
  }
 }else if(g->hp>0){
  int dx=g->x-owner->x,dy=g->y-owner->y;
  if(dx>=-1&&dx<=1&&dy>=-1&&dy<=1){*tx=g->x;*ty=g->y;}
 }
}
void game_wall_traps_step(Game *g){
 if(g->area)return;
 static const uint8_t missiles[3]={GAME_TRAP_ARROW,GAME_TRAP_FIREBOLT,GAME_TRAP_LIGHTNING};
 for(unsigned i=0;i<MAX_PROPS;i++){
  Prop *p=&g->props[i];if(!wall_record(p)||(p->state&WALL_TRAP_SPENT))continue;
  unsigned target=p->state&63;const Prop *owner=&g->props[target];
  unsigned axis=(p->kind-PROP_WALL_TRAP_FIRST)/3;
  /* A missing/replaced invalid owner never attaches to a nearby substitute. */
  if(!wall_target_kind(owner->kind)||owner->x>=80||owner->y>=80||
     (axis?(owner->x!=p->x||(int)owner->y-p->y<2):(owner->y!=p->y||(int)owner->x-p->x<2))||
     (g->trap_spent&((uint64_t)1<<target))){p->state|=WALL_TRAP_SPENT;continue;}
  if(!owner->state)continue;
  /* Retail consumes the trigger even when AddMissile finds its pool full. */
  p->state|=WALL_TRAP_SPENT;
  int tx,ty;wall_aim(g,owner,&tx,&ty);
  uint32_t seed=campaign_seed^dungeon_level*0x9e3779b9u^(p->y*80+p->x+1u)*0x74726170u^target;
  game_trap_fire(g,p->x,p->y,tx,ty,missiles[(p->kind-PROP_WALL_TRAP_FIRST)%3],seed);
  game_hazard_sound(0);
 }
}
