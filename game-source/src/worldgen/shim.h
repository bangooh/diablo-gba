#pragma once
#include <stdint.h>
#include "fast_seed.h"
#include <stddef.h>
#include "../world_rooms.h"
#include "../theme_rooms.h"
typedef uint8_t BYTE,BOOLEAN;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DMAXX 40
#define DMAXY 40
#define MAXDUNX 112
#define MAXDUNY 112
#define ENTRY_MAIN 0
#define ENTRY_PREV 1
#define DLRG_PROTECTED 128
#define QUEST_ACTIVE 2
#define QUEST_NOTAVAIL 0
#define Q_BLIND 0
#define Q_BLOOD 1
#define Q_ANVIL 2
#define Q_WARLORD 3
#define Q_BETRAYER 4
#define Q_DIABLO 5
#define Q_SKELKING 6
#define Q_SCHAMB 7
#define DTYPE_CATHEDRAL 1
#define DTYPE_CATACOMBS 2
#define DTYPE_CAVES 3
#define DTYPE_HELL 4
typedef struct { BYTE strig,s1,s2,s3,nv1,nv2,nv3; } ShadowStruct;
typedef struct HALLNODE { int nHallx1,nHally1,nHallx2,nHally2,nHalldir; struct HALLNODE *pNext; } HALLNODE;
typedef struct { int nRoomx1,nRoomy1,nRoomx2,nRoomy2,nRoomDest; } ROOMNODE;
typedef struct { int _qactive,_qlevel,_qtx,_qty; } Quest;
extern uint8_t *dungeon_workspace;
extern uint8_t game_cave_policy;
typedef struct {
 uint8_t map[2304],flags[2304],prior[40][40];
 Quest quest[8];
 int level,ViewX,ViewY,LvlViewX,LvlViewY,setpc_x,setpc_y,setpc_w,setpc_h;
 int setloadflag,gbMaxPlayers,themeCount,leveltype;
 BYTE *pSetPiece; uint32_t seed; unsigned heap_used;
 int up_x,up_y,down_x,down_y;
 WorldThemeRoom theme_rooms[WORLD_THEME_LIMIT];
} WorldState;
#define ws ((WorldState *)(dungeon_workspace+30000))
#define dungeon ((BYTE (*)[48])(ws->map+196))
#define dflags ((BYTE (*)[48])(ws->flags+196))
#define pdungeon ws->prior
#define quests ws->quest
#define currlevel ws->level
#define ViewX ws->ViewX
#define ViewY ws->ViewY
#define LvlViewX ws->LvlViewX
#define LvlViewY ws->LvlViewY
#define setpc_x ws->setpc_x
#define setpc_y ws->setpc_y
#define setpc_w ws->setpc_w
#define setpc_h ws->setpc_h
#define setloadflag ws->setloadflag
#define gbMaxPlayers ws->gbMaxPlayers
#define themeCount ws->themeCount
#define leveltype ws->leveltype
#define pSetPiece ws->pSetPiece
void *memset(void *,int,size_t);
void *memcpy(void *,const void *,size_t);
int abs(int);
int random_(int,int);
#define QuestStatus(q) ((((q)==Q_ANVIL && currlevel==10)||((q)==Q_BLOOD && currlevel==5)||((q)==Q_BLIND && currlevel==7)||((q)==Q_SCHAMB && currlevel==6)||((q)==Q_WARLORD && currlevel==13)) && quests[q]._qactive)
#define DRLG_InitTrans() game_rooms_begin()
#define DRLG_Init_Globals() ((void)0)
void DRLG_CheckQuests(int x,int y);
void DRLG_PlaceThemeRooms(int minSize,int maxSize,int floor,int freq,int rndSize);
int SkipThemeRoom(int x,int y);
#define DRLG_RectTrans(a,b,c,d) game_rooms_rect(a,b,c,d,-1)
#define Make_SetPC(a,b,c,d) ((void)0)
void *DiabloAllocPtr(unsigned);
void mem_free_dbg(void *);
BYTE *LoadFileInMem(const char *,void *);
void worldgen_l2(void);
void worldgen_l3(void);
void worldgen_l4(void);
#define MemFreeDbg mem_free_dbg
