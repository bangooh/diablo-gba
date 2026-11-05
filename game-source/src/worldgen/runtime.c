#include "fast_seed.h"
uint32_t world_generation_work;
#include "../../generated/floor_fast_seeds79.h"
#ifdef GEN_PROFILE
unsigned gen_attempts;uint32_t gen_candidate_seed;
#endif
uint32_t world_fast_seed(unsigned level,unsigned variant,unsigned index){
 static const uint8_t offsets[16]={0,1,5,7,9,11,13,15,16,17,19,20,21,23,24,25};
 return floor_fast_seeds79[offsets[level-1]+variant][index%120];
}
#include "shim.h"
_Static_assert(sizeof(WorldState)<10000,"world metadata overlaps generator scratch");
#include "../../generated/diablo_quads.inc"
#include "../../generated/valor_room.h"
#include "../../generated/blind_room.h"
#include "../../generated/late_quests.h"
#include "../world_rooms.inc"
int random_(int index,int v){
 (void)index;if(v<=0)return 0;GEN_WORK();
 ws->seed=ws->seed*0x015a4e35u+1;
 uint32_t n=ws->seed;if(n&0x80000000u)n=0u-n;
 return (int)((v<65535?n>>16:n)%(unsigned)v);
}
int abs(int n){return n<0?-n:n;}
#include "theme_arch.inc"
void *DiabloAllocPtr(unsigned n){
 if(n>64)return 0;
 for(unsigned i=0;i<256;i++)if(!dungeon_workspace[63000+i]){dungeon_workspace[63000+i]=1;return dungeon_workspace+64000+i*64;}
 return 0;
}
void mem_free_dbg(void *p){
 uintptr_t a=(uintptr_t)p,b=(uintptr_t)(dungeon_workspace+64000);
 if(a>=b&&a<b+16384)dungeon_workspace[63000+(a-b)/64]=0;
}
BYTE *LoadFileInMem(const char *name,void *unused){
 (void)unused;const char *s=name;for(const char *p=name;*p;p++)if(*p=='\\')s=p+1;
 const BYTE *data=diab1;
 if(s[4]=='2')data=s[5]=='a'?diab2a:diab2b;
 if(s[4]=='3')data=s[5]=='a'?diab3a:diab3b;
 if(s[4]=='4')data=s[5]=='a'?diab4a:diab4b;
 return (BYTE*)data;
}
/* Native DrawBlood overlays the closed Blood2 room AFTER pdungeon has
 * captured Blood1. ObjChangeMap restores that prior map, including inclusive
 * rectangle edges. Reading Blood1 directly here would lose L2 decorations. */
static void valor_restore(int x1,int y1,int x2,int y2){
 for(int y=y1;y<=y2;y++)for(int x=x1;x<=x2;x++)dungeon[x][y]=pdungeon[x][y];
}
void DRLG_CheckQuests(int x,int y){
 if(QuestStatus(Q_SCHAMB)||QuestStatus(Q_WARLORD)){const BYTE *p=QuestStatus(Q_SCHAMB)?late_bonestr1:late_warlord2;unsigned q=QuestStatus(Q_SCHAMB)?Q_SCHAMB:Q_WARLORD;for(unsigned j=0;j<p[2];j++)for(unsigned i=0;i<p[0];i++){unsigned tile=p[4+2*(j*p[0]+i)];dungeon[x+i][y+j]=tile?tile:(q==Q_SCHAMB?3:6);}if(quests[q]._qtx)valor_restore(x,y,x+p[0]-1,y+p[2]-1);return;}
 if(QuestStatus(Q_BLIND)){
  for(int j=0;j<11;j++)for(int i=0;i<11;i++){unsigned tile=blind_blind1[4+2*(j*11+i)];if(tile)pdungeon[x+i][y+j]=tile;}
  if(quests[Q_BLIND]._qlevel){
   valor_restore(x,y,x+12,y+12);
   /* OperateBookLever/SyncObjectAnim use TransVal 9 for this native range.
    * Reapply to a regenerated candidate as well as the live opening event. */
   game_rooms_macro_rect(x,y,x+12,y+12,9);
  }
  return;
 }
 if(!QuestStatus(Q_BLOOD))return;
 setpc_x=x;setpc_y=y;setpc_w=valor_blood2[0];setpc_h=valor_blood2[2];
 for(int j=0;j<setpc_h;j++)for(int i=0;i<setpc_w;i++){
  unsigned tile=valor_blood2[4+2*(j*setpc_w+i)];if(tile)dungeon[x+i][y+j]=tile;
 }
 unsigned stones=(unsigned)quests[Q_BLOOD]._qlevel;
 if(stones>=1)valor_restore(x,y+3,x+2,y+7);
 if(stones>=2)valor_restore(x+6,y+3,x+setpc_w,y+7);
 if(stones>=3)valor_restore(x,y,x+setpc_w,y+setpc_h);
}
static void generate(unsigned level,uint32_t seed,uint8_t *out,int *up,int *down,int anvil,int *cell,int valor,unsigned stones,int *origin){
 memset(ws,0,sizeof(*ws));memset(dungeon_workspace+63000,0,256);
 /* L3 has no flood: its native default remains a valid all-zero map.
  * L2/L4 also reset this candidate at the start of each generation retry. */
 game_rooms_begin();
 currlevel=level;ws->seed=seed;gbMaxPlayers=1;leveltype=(level-1)/4+1;
 quests[Q_ANVIL]._qactive=anvil&&level==10;
 if(valor==3||valor==4){unsigned q=valor==3?Q_SCHAMB:Q_WARLORD;quests[q]._qactive=QUEST_ACTIVE;quests[q]._qlevel=level;quests[q]._qtx=!!stones;pSetPiece=(BYTE*)(valor==3?late_bonestr2:late_warlord);setloadflag=TRUE;}
 if(valor==2&&level==7){quests[Q_BLIND]._qactive=QUEST_ACTIVE;quests[Q_BLIND]._qlevel=!!stones;pSetPiece=(BYTE*)blind_blind2;setloadflag=TRUE;}
 if(valor==1&&level==5){
  quests[Q_BLOOD]._qactive=QUEST_ACTIVE;quests[Q_BLOOD]._qlevel=stones>3?3:stones;
  pSetPiece=(BYTE*)valor_blood1;setloadflag=TRUE;
 }
 if(level<9)worldgen_l2();else if(level<13)worldgen_l3();else worldgen_l4();
 for(int y=0;y<40;y++)for(int x=0;x<40;x++)out[y*40+x]=dungeon[x][y];
 if(cell)*cell=anvil&&level==10?(2*setpc_y+11)*80+2*setpc_x+11:0;
 if(origin){origin[0]=setpc_x;origin[1]=setpc_y;}
 up[0]=ws->up_x;up[1]=ws->up_y;down[0]=ws->down_x;down[1]=ws->down_y;
}
void world_generate_quest(unsigned level,uint32_t seed,uint8_t *out,int *up,int *down,int anvil,int *cell){generate(level,seed,out,up,down,anvil,cell,0,0,0);}
void world_generate_valor(uint32_t seed,unsigned stones,uint8_t *out,int *up,int *down,int *origin){generate(5,seed,out,up,down,0,0,1,stones,origin);}
void world_valor_open_tiles(uint8_t *out){for(int y=0;y<=16;y++)for(int x=0;x<=10;x++)out[y*11+x]=pdungeon[setpc_x+x][setpc_y+y];}
void world_generate_blind(uint32_t seed,unsigned opened,uint8_t *out,int *up,int *down,int *origin){generate(7,seed,out,up,down,0,0,2,opened,origin);}
void world_blind_open_tiles(uint8_t *out){for(int y=0;y<13;y++)for(int x=0;x<13;x++)out[y*13+x]=pdungeon[setpc_x+x][setpc_y+y];}

void world_generate(unsigned level,uint32_t seed,uint8_t *out,int *up,int *down){world_generate_quest(level,seed,out,up,down,0,0);}

void world_generate_late(unsigned level,uint32_t seed,unsigned opened,uint8_t *out,int *up,int *down,int *origin){generate(level,seed,out,up,down,0,0,level==6?3:4,opened,origin);if(level==13){down[0]=setpc_x*2+6;down[1]=setpc_y*2+6;}}
void world_late_open_tiles(uint8_t *out,unsigned width,unsigned height){for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)out[y*width+x]=pdungeon[setpc_x+x][setpc_y+y];}
