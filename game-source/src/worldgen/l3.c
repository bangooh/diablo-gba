/* Modified GBA adaptation of Devilution Source/drlg_l3.cpp.
 * Reference revision: 9f01757243412bb154d02d9af65c804159e6e0ea.
 * Original reference bytes verified; see docs/upstream-comparison.json.
 * Changes include bounded generation state, GBA RNG/instrumentation and map access.
 * Retains Sustainable Use License; see LICENSES/Devilution-LICENSE.md and NOTICE.md.
 * Function comparison is lexical evidence, not a claim of semantic equivalence.
 */
#include "shim.h"
#ifdef L3_PROFILE
unsigned long l3_profile[8];
#define L3_COUNT(n) (++l3_profile[n])
#define L3_SEED() (l3_profile[7]=ws->seed)
#else
#define L3_COUNT(n) ((void)0)
#define L3_SEED() ((void)0)
#endif
/* Keep the exact native RNG stream, but expose constant bounds to ARM/O2.
 * Repeated ARM -> Thumb helper calls otherwise also hide cheap constant
 * remainders behind a general software division on ARM7TDMI. L3 only. */
static inline int l3_random(int index,int v){
 (void)index;if(v<=0)return 0;GEN_WORK();L3_COUNT(6);
 ws->seed=ws->seed*0x015a4e35u+1;
 uint32_t n=ws->seed;if(n&0x80000000u)n=0u-n;
 return (int)((v<65535?n>>16:n)%(unsigned)v);
}
#define random_ l3_random
/* Preserve allocated border probes without indexing before a C row. */
#define L3CELL(x,y) (ws->map[196+(x)*48+(y)])




static const BYTE L3ConvTbl[16] = { 8, 11, 3, 10, 1, 9, 12, 12, 6, 13, 4, 13, 2, 14, 5, 7 };
static const BYTE L3UP[] = {
 3, 3,
  8, 8, 0,
 10, 10, 0,
  7, 7, 0,
 51, 50, 0,
 48, 49, 0,
  0, 0, 0,
};
static const BYTE L3DOWN[] = {
 3, 3,
 8, 9, 7,
 8, 9, 7,
 0, 0, 0,
 0, 47, 0,
 0, 46, 0,
 0, 0, 0,
};
static const BYTE L3HOLDWARP[] = {
 3, 3,
  8, 8, 0,
 10, 10, 0,
  7, 7, 0,
 125, 125, 0,
 125, 125, 0,
   0, 0, 0,
};
static const BYTE L3TITE1[] = {
 4, 4,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 0, 0, 0, 0,
 0, 57, 58, 0,
 0, 56, 55, 0,
 0, 0, 0, 0,
};
static const BYTE L3TITE2[] = {
 4, 4,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 0, 0, 0, 0,
 0, 61, 62, 0,
 0, 60, 59, 0,
 0, 0, 0, 0,
};
static const BYTE L3TITE3[] = {
 4, 4,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 0, 0, 0, 0,
 0, 65, 66, 0,
 0, 64, 63, 0,
 0, 0, 0, 0,
};
static const BYTE L3TITE6[] = {
 5, 4,
 7, 7, 7, 7, 7,
 7, 7, 7, 0, 7,
 7, 7, 7, 0, 7,
 7, 7, 7, 7, 7,
 0, 0, 0, 0, 0,
 0, 77, 78, 0, 0,
 0, 76, 74, 75, 0,
 0, 0, 0, 0, 0,
};
static const BYTE L3TITE7[] = {
 4, 5,
 7, 7, 7, 7,
 7, 7, 0, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 7, 7, 7, 7,
 0, 0, 0, 0,
 0, 83, 0, 0,
 0, 82, 80, 0,
 0, 81, 79, 0,
 0, 0, 0, 0,
};
static const BYTE L3TITE8[] = {
 3, 3,
 7, 7, 7,
 7, 7, 7,
 7, 7, 7,
 0, 0, 0,
 0, 52, 0,
 0, 0, 0,
};
static const BYTE L3TITE9[] = {
 3, 3,
 7, 7, 7,
 7, 7, 7,
 7, 7, 7,
 0, 0, 0,
 0, 53, 0,
 0, 0, 0,
};
static const BYTE L3TITE10[] = {
 3, 3,
 7, 7, 7,
 7, 7, 7,
 7, 7, 7,
 0, 0, 0,
 0, 54, 0,
 0, 0, 0,
};
static const BYTE L3TITE11[] = {
 3, 3,
 7, 7, 7,
 7, 7, 7,
 7, 7, 7,
 0, 0, 0,
 0, 67, 0,
 0, 0, 0,
};
static const BYTE L3TITE12[] = {
 2, 1,
 9, 7,
 68, 0,
};
static const BYTE L3TITE13[] = {
 1, 2,
 10,
  7,
 69,
  0,
};
static const BYTE L3CREV1[] = {
 2, 1,
 8, 7,
 84, 85,
};
static const BYTE L3CREV2[] = {
 2, 1,
 8, 11,
 86, 87,
};
static const BYTE L3CREV3[] = {
 1, 2,
  8,
 10,
 89,
 88,
};
static const BYTE L3CREV4[] = {
 2, 1,
 8, 7,
 90, 91,
};
static const BYTE L3CREV5[] = {
 1, 2,
  8,
 11,
 92,
 93,
};
static const BYTE L3CREV6[] = {
 1, 2,
  8,
 10,
 95,
 94,
};
static const BYTE L3CREV7[] = {
 2, 1,
 8, 7,
 96, 101,
};
static const BYTE L3CREV8[] = {
 1, 2,
 2,
 8,
 102,
  97,
};
static const BYTE L3CREV9[] = {
 2, 1,
 3, 8,
 103, 98,
};
static const BYTE L3CREV10[] = {
 2, 1,
 4, 8,
 104, 99,
};
static const BYTE L3CREV11[] = {
 1, 2,
 6,
 8,
 105,
 100,
};
static const BYTE L3ISLE1[] = {
 2, 3,
 5, 14,
 4, 9,
 13, 12,
 7, 7,
 7, 7,
 7, 7,
};
static const BYTE L3ISLE2[] = {
 3, 2,
  5, 2, 14,
 13, 10, 12,
 7, 7, 7,
 7, 7, 7,
};
static const BYTE L3ISLE3[] = {
 2, 3,
  5, 14,
  4, 9,
 13, 12,
 29, 30,
 25, 28,
 31, 32,
};
static const BYTE L3ISLE4[] = {
 3, 2,
  5, 2, 14,
 13, 10, 12,
 29, 26, 30,
 31, 27, 32,
};
static const BYTE L3ISLE5[] = {
 2, 2,
  5, 14,
 13, 12,
 7, 7,
 7, 7,
};
static const BYTE L3XTRA1[] = {
 1, 1,
 7,
 106,
};
static const BYTE L3XTRA2[] = {
 1, 1,
 7,
 107,
};
static const BYTE L3XTRA3[] = {
 1, 1,
 7,
 108,
};
static const BYTE L3XTRA4[] = {
 1, 1,
 9,
 109,
};
static const BYTE L3XTRA5[] = {
 1, 1,
 10,
 110,
};
static const BYTE L3ANVIL[] = {
 11, 11,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 29, 26, 26, 26, 26, 26, 30, 0, 0,
 0, 29, 34, 33, 33, 37, 36, 33, 35, 30, 0,
 0, 25, 33, 37, 27, 32, 31, 36, 33, 28, 0,
 0, 25, 37, 32, 7, 7, 7, 31, 27, 32, 0,
 0, 25, 28, 7, 7, 7, 7, 2, 2, 2, 0,
 0, 25, 35, 30, 7, 7, 7, 29, 26, 30, 0,
 0, 25, 33, 35, 26, 30, 29, 34, 33, 28, 0,
 0, 31, 36, 33, 33, 35, 34, 33, 37, 32, 0,
 0, 0, 31, 27, 27, 27, 27, 27, 32, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

struct LocalState { BOOLEAN lavapool; int abyssx; int lockoutcnt; BOOLEAN lockout[DMAXX][DMAXY]; };
#define local_state ((struct LocalState *)(dungeon_workspace+40000))
#define lavapool (local_state->lavapool)
#define abyssx (local_state->abyssx)
#define lockoutcnt (local_state->lockoutcnt)
#define lockout (local_state->lockout)
static void InitL3Dungeon();
static BOOL DRLG_L3FillRoom(int x1, int y1, int x2, int y2);
static void DRLG_L3CreateBlock(int x, int y, int obs, int dir);
static void DRLG_L3FloorArea(int x1, int y1, int x2, int y2);
static void DRLG_L3FillDiags();
static void DRLG_L3FillSingles();
static void DRLG_L3FillStraights();
static void DRLG_L3Edges();
static int DRLG_L3GetFloorArea();
static void DRLG_L3MakeMegas();
static __attribute__((noinline)) void DRLG_L3River();
static BOOL DRLG_L3SpawnEdge(int x, int y, int *totarea);
static BOOL DRLG_L3Spawn(int x, int y, int *totarea);
static void DRLG_L3Pool();
static void DRLG_L3PoolFix();
static BOOL DRLG_L3PlaceMiniSet(const BYTE *miniset, int tmin, int tmax, int cx, int cy, BOOL setview, int ldir);
static void DRLG_L3PlaceRndSet(const BYTE *miniset, int rndper);
static BOOL WoodVertU(int i, int y);
static BOOL WoodVertD(int i, int y);
static BOOL WoodHorizL(int x, int j);
static BOOL WoodHorizR(int x, int j);
void AddFenceDoors();
void FenceDoorFix();
static void DRLG_L3Wood();
BOOL DRLG_L3Anvil();
void FixL3Warp();
void FixL3HallofHeroes();
void DRLG_L3LockRec(int x, int y);
BOOL DRLG_L3Lockout();
static void DRLG_L3(int entry);
static void InitL3Dungeon()
{
 int i, j;
 memset(ws->map, 0, sizeof(ws->map));
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   L3CELL(i,j) = 0;
   dflags[i][j] = 0;
  }
 }
}
static BOOL DRLG_L3FillRoom(int x1, int y1, int x2, int y2)
{
 int i, j, v;
 if (x1 <= 1 || x2 >= 34 || y1 <= 1 || y2 >= 38) {
  return FALSE;
 }
 v = 0;
 for (j = y1; j <= y2; j++) {
  for (i = x1; i <= x2; i++) {
   v += L3CELL(i,j);
  }
 }
 if (v != 0) {
  return FALSE;
 }
 for (j = y1 + 1; j < y2; j++) {
  for (i = x1 + 1; i < x2; i++) {
   L3CELL(i,j) = 1;
  }
 }
 for (j = y1; j <= y2; j++) {
  if (random_(0, 2) != 0) {
   L3CELL(x1,j) = 1;
  }
  if (random_(0, 2) != 0) {
   L3CELL(x2,j) = 1;
  }
 }
 for (i = x1; i <= x2; i++) {
  if (random_(0, 2) != 0) {
   L3CELL(i,y1) = 1;
  }
  if (random_(0, 2) != 0) {
   L3CELL(i,y2) = 1;
  }
 }
 return TRUE;
}
/* Room growth owns this arena only until its DFS is exhausted. FillRoom
 * admits interiors inside30x34 cells, writes at least four new cells per
 * success, and never clears them: at most255 successes. A pop pushes at most
 * three children, so the frontier is at most1+2*255=511 tasks. */
typedef struct { int16_t x,y,obs,dir; } L3BlockTask;
enum { L3_BLOCK_TASK_OFFSET=48000,L3_BLOCK_TASK_CAPACITY=512 };
_Static_assert(sizeof(L3BlockTask)==8,"L3 block task layout");
_Static_assert(40000+sizeof(struct LocalState)<=L3_BLOCK_TASK_OFFSET,"L3 local state before DFS arena");
_Static_assert(L3_BLOCK_TASK_OFFSET+L3_BLOCK_TASK_CAPACITY*sizeof(L3BlockTask)<=63000,"L3 DFS before heap flags");
_Static_assert(1+2*(30*34/4)<=L3_BLOCK_TASK_CAPACITY,"L3 DFS frontier capacity");
static void DRLG_L3CreateBlock(int x, int y, int obs, int dir)
{
 L3BlockTask *todo=(void*)(dungeon_workspace+L3_BLOCK_TASK_OFFSET);
 unsigned count=1;todo[0]=(L3BlockTask){x,y,obs,dir};
 while(count){
  L3BlockTask task=todo[--count];x=task.x;y=task.y;obs=task.obs;dir=task.dir;
  int blksizex, blksizey, x1, y1, x2, y2;
  int contflag;
  blksizex = random_(0, 2) + 3;
  blksizey = random_(0, 2) + 3;
  if (dir == 0) {
   y2 = y - 1;
   y1 = y2 - blksizey;
   if (blksizex < obs) {
    x1 = random_(0, blksizex) + x;
   }
   if (blksizex == obs) {
    x1 = x;
   }
   if (blksizex > obs) {
    x1 = x - random_(0, blksizex);
   }
   x2 = blksizex + x1;
  }
  if (dir == 3) {
   x2 = x - 1;
   x1 = x2 - blksizex;
   if (blksizey < obs) {
    y1 = random_(0, blksizey) + y;
   }
   if (blksizey == obs) {
    y1 = y;
   }
   if (blksizey > obs) {
    y1 = y - random_(0, blksizey);
   }
   y2 = y1 + blksizey;
  }
  if (dir == 2) {
   y1 = y + 1;
   y2 = y1 + blksizey;
   if (blksizex < obs) {
    x1 = random_(0, blksizex) + x;
   }
   if (blksizex == obs) {
    x1 = x;
   }
   if (blksizex > obs) {
    x1 = x - random_(0, blksizex);
   }
   x2 = blksizex + x1;
  }
  if (dir == 1) {
   x1 = x + 1;
   x2 = x1 + blksizex;
   if (blksizey < obs) {
    y1 = random_(0, blksizey) + y;
   }
   if (blksizey == obs) {
    y1 = y;
   }
   if (blksizey > obs) {
    y1 = y - random_(0, blksizey);
   }
   y2 = y1 + blksizey;
  }
  if (DRLG_L3FillRoom(x1, y1, x2, y2) == TRUE) {
   contflag = random_(0, 4);
   /* LIFO visits the original0,1,2,3 order; each child still draws its
    * random dimensions only when popped, after the preceding subtree. */
   if (contflag != 0 && dir != 1)
    todo[count++] = (L3BlockTask){ x1, y1, blksizex, 3 };
   if (contflag != 0 && dir != 0)
    todo[count++] = (L3BlockTask){ x1, y2, blksizey, 2 };
   if (contflag != 0 && dir != 3)
    todo[count++] = (L3BlockTask){ x2, y1, blksizex, 1 };
   if (contflag != 0 && dir != 2)
    todo[count++] = (L3BlockTask){ x1, y1, blksizey, 0 };
  }
 }
}
static void DRLG_L3FloorArea(int x1, int y1, int x2, int y2)
{
 int i, j;
 for (j = y1; j <= y2; j++) {
  for (i = x1; i <= x2; i++) {
   L3CELL(i,j) = 1;
  }
 }
}
static void DRLG_L3FillDiags()
{
 int i, j, v;
 for (j = 0; j < DMAXY - 1; j++) {
  for (i = 0; i < DMAXX - 1; i++) {
   v = L3CELL(i + 1,j + 1) + 2 * L3CELL(i,j + 1) + 4 * L3CELL(i + 1,j) + 8 * L3CELL(i,j);
   if (v == 6) {
    if (random_(0, 2) == 0) {
     L3CELL(i,j) = 1;
    } else {
     L3CELL(i + 1,j + 1) = 1;
    }
   }
   if (v == 9) {
    if (random_(0, 2) == 0) {
     L3CELL(i + 1,j) = 1;
    } else {
     L3CELL(i,j + 1) = 1;
    }
   }
  }
 }
}
static void DRLG_L3FillSingles()
{
 int i, j;
 for (j = 1; j < DMAXY - 1; j++) {
  for (i = 1; i < DMAXX - 1; i++) {
   if (L3CELL(i,j) == 0
       && L3CELL(i,j - 1) + L3CELL(i - 1,j - 1) + L3CELL(i + 1,j - 1) == 3
       && L3CELL(i + 1,j) + L3CELL(i - 1,j) == 2
       && L3CELL(i,j + 1) + L3CELL(i - 1,j + 1) + L3CELL(i + 1,j + 1) == 3) {
    L3CELL(i,j) = 1;
   }
  }
 }
}
static void DRLG_L3FillStraights()
{
 int i, j, xc, xs, yc, ys, k, rv;
 for (j = 0; j < DMAXY - 1; j++) {
  xs = 0;
  for (i = 0; i < 37; i++) {
   if (L3CELL(i,j) == 0 && L3CELL(i,j + 1) == 1) {
    if (xs == 0) {
     xc = i;
    }
    xs++;
   } else {
    if (xs > 3 && random_(0, 2) != 0) {
     for (k = xc; k < i; k++) {
      rv = random_(0, 2);
      L3CELL(k,j) = rv;
     }
    }
    xs = 0;
   }
  }
 }
 for (j = 0; j < DMAXY - 1; j++) {
  xs = 0;
  for (i = 0; i < 37; i++) {
   if (L3CELL(i,j) == 1 && L3CELL(i,j + 1) == 0) {
    if (xs == 0) {
     xc = i;
    }
    xs++;
   } else {
    if (xs > 3 && random_(0, 2) != 0) {
     for (k = xc; k < i; k++) {
      rv = random_(0, 2);
      L3CELL(k,j + 1) = rv;
     }
    }
    xs = 0;
   }
  }
 }
 for (i = 0; i < DMAXX - 1; i++) {
  ys = 0;
  for (j = 0; j < 37; j++) {
   if (L3CELL(i,j) == 0 && L3CELL(i + 1,j) == 1) {
    if (ys == 0) {
     yc = j;
    }
    ys++;
   } else {
    if (ys > 3 && random_(0, 2) != 0) {
     for (k = yc; k < j; k++) {
      rv = random_(0, 2);
      L3CELL(i,k) = rv;
     }
    }
    ys = 0;
   }
  }
 }
 for (i = 0; i < DMAXX - 1; i++) {
  ys = 0;
  for (j = 0; j < 37; j++) {
   if (L3CELL(i,j) == 1 && L3CELL(i + 1,j) == 0) {
    if (ys == 0) {
     yc = j;
    }
    ys++;
   } else {
    if (ys > 3 && random_(0, 2) != 0) {
     for (k = yc; k < j; k++) {
      rv = random_(0, 2);
      L3CELL(i + 1,k) = rv;
     }
    }
    ys = 0;
   }
  }
 }
}
static void DRLG_L3Edges()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  L3CELL(DMAXX - 1,j) = 0;
 }
 for (i = 0; i < DMAXX; i++) {
  L3CELL(i,DMAXY - 1) = 0;
 }
}
static int DRLG_L3GetFloorArea()
{
 int i, j, gfa;
 gfa = 0;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   gfa += L3CELL(i,j);
  }
 }
 return gfa;
}
static void DRLG_L3MakeMegas()
{
 int i, j, v, rv;
 for (j = 0; j < DMAXY - 1; j++) {
  for (i = 0; i < DMAXX - 1; i++) {
   v = L3CELL(i + 1,j + 1) + 2 * L3CELL(i,j + 1) + 4 * L3CELL(i + 1,j) + 8 * L3CELL(i,j);
   if (v == 6) {
    rv = random_(0, 2);
    if (rv == 0) {
     v = 12;
    } else {
     v = 5;
    }
   }
   if (v == 9) {
    rv = random_(0, 2);
    if (rv == 0) {
     v = 13;
    } else {
     v = 14;
    }
   }
   L3CELL(i,j) = L3ConvTbl[v];
  }
  L3CELL(DMAXX - 1,j) = 8;
 }
 for (i = 0; i < DMAXX; i++) {
  L3CELL(i,DMAXY - 1) = 8;
 }
}
static __attribute__((noinline)) void DRLG_L3River()
{
 int rx, ry, px, py, dir, pdir, nodir, nodir2, dircheck;
 int river[3][100];
 int rivercnt, riveramt;
 int i, trys, found, bridge, lpcnt;
 BOOL bail;
 rivercnt = 0;
 bail = FALSE;
 trys = 0;
 while (trys < 200 && rivercnt < 4) {
  bail = FALSE;
  while (!bail && trys < 200) {
   trys++;
   rx = 0;
   ry = 0;
   i = 0;
   while ((L3CELL(rx,ry) < 25 || L3CELL(rx,ry) > 28) && i < 100) {
    rx = random_(0, DMAXX);
    ry = random_(0, DMAXY);
    i++;
    while ((L3CELL(rx,ry) < 25 || L3CELL(rx,ry) > 28) && ry < DMAXY) {
     rx++;
     if (rx >= DMAXX) {
      rx = 0;
      ry++;
     }
    }
   }
   if (i >= 100) {
    return;
   }
   switch (L3CELL(rx,ry)) {
   case 25:
    dir = 3;
    nodir = 2;
    river[2][0] = 40;
    break;
   case 26:
    dir = 0;
    nodir = 1;
    river[2][0] = 38;
    break;
   case 27:
    dir = 1;
    nodir = 0;
    river[2][0] = 41;
    break;
   case 28:
    dir = 2;
    nodir = 3;
    river[2][0] = 39;
    break;
   }
   river[0][0] = rx;
   river[1][0] = ry;
   riveramt = 1;
   nodir2 = 4;
   dircheck = 0;
   while (dircheck < 4 && riveramt < 100) {
    px = rx;
    py = ry;
    if (dircheck == 0) {
     dir = random_(0, 4);
    } else {
     dir = (dir + 1) & 3;
    }
    dircheck++;
    while (dir == nodir || dir == nodir2) {
     dir = (dir + 1) & 3;
     dircheck++;
    }
    if (dir == 0 && ry > 0) {
     ry--;
    }
    if (dir == 1 && ry < DMAXY) {
     ry++;
    }
    if (dir == 2 && rx < DMAXX) {
     rx++;
    }
    if (dir == 3 && rx > 0) {
     rx--;
    }
    if (L3CELL(rx,ry) == 7) {
     dircheck = 0;
     if (dir < 2) {
      river[2][riveramt] = (BYTE)random_(0, 2) + 17;
     }
     if (dir > 1) {
      river[2][riveramt] = (BYTE)random_(0, 2) + 15;
     }
     river[0][riveramt] = rx;
     river[1][riveramt] = ry;
     riveramt++;
     if (dir == 0 && pdir == 2 || dir == 3 && pdir == 1) {
      if (riveramt > 2) {
       river[2][riveramt - 2] = 22;
      }
      if (dir == 0) {
       nodir2 = 1;
      } else {
       nodir2 = 2;
      }
     }
     if (dir == 0 && pdir == 3 || dir == 2 && pdir == 1) {
      if (riveramt > 2) {
       river[2][riveramt - 2] = 21;
      }
      if (dir == 0) {
       nodir2 = 1;
      } else {
       nodir2 = 3;
      }
     }
     if (dir == 1 && pdir == 2 || dir == 3 && pdir == 0) {
      if (riveramt > 2) {
       river[2][riveramt - 2] = 20;
      }
      if (dir == 1) {
       nodir2 = 0;
      } else {
       nodir2 = 2;
      }
     }
     if (dir == 1 && pdir == 3 || dir == 2 && pdir == 0) {
      if (riveramt > 2) {
       river[2][riveramt - 2] = 19;
      }
      if (dir == 1) {
       nodir2 = 0;
      } else {
       nodir2 = 3;
      }
     }
     pdir = dir;
    } else {
     rx = px;
     ry = py;
    }
   }
   if (dir == 0 && L3CELL(rx,ry - 1) == 10 && L3CELL(rx,ry - 2) == 8) {
    river[0][riveramt] = rx;
    river[1][riveramt] = ry - 1;
    river[2][riveramt] = 24;
    if (pdir == 2) {
     river[2][riveramt - 1] = 22;
    }
    if (pdir == 3) {
     river[2][riveramt - 1] = 21;
    }
    bail = TRUE;
   }
   if (dir == 1 && L3CELL(rx,ry + 1) == 2 && L3CELL(rx,ry + 2) == 8) {
    river[0][riveramt] = rx;
    river[1][riveramt] = ry + 1;
    river[2][riveramt] = 42;
    if (pdir == 2) {
     river[2][riveramt - 1] = 20;
    }
    if (pdir == 3) {
     river[2][riveramt - 1] = 19;
    }
    bail = TRUE;
   }
   if (dir == 2 && L3CELL(rx + 1,ry) == 4 && L3CELL(rx + 2,ry) == 8) {
    river[0][riveramt] = rx + 1;
    river[1][riveramt] = ry;
    river[2][riveramt] = 43;
    if (pdir == 0) {
     river[2][riveramt - 1] = 19;
    }
    if (pdir == 1) {
     river[2][riveramt - 1] = 21;
    }
    bail = TRUE;
   }
   if (dir == 3 && L3CELL(rx - 1,ry) == 9 && L3CELL(rx - 2,ry) == 8) {
    river[0][riveramt] = rx - 1;
    river[1][riveramt] = ry;
    river[2][riveramt] = 23;
    if (pdir == 0) {
     river[2][riveramt - 1] = 20;
    }
    if (pdir == 1) {
     river[2][riveramt - 1] = 22;
    }
    bail = TRUE;
   }
  }
  if (bail == TRUE && riveramt < 7) {
   bail = FALSE;
  }
  if (bail == TRUE) {
   found = 0;
   lpcnt = 0;
   while (found == 0 && lpcnt < 30) {
    lpcnt++;
    bridge = random_(0, riveramt);
    if ((river[2][bridge] == 15 || river[2][bridge] == 16)
        && L3CELL(river[0][bridge],river[1][bridge] - 1) == 7
        && L3CELL(river[0][bridge],river[1][bridge] + 1) == 7) {
     found = 1;
    }
    if ((river[2][bridge] == 17 || river[2][bridge] == 18)
        && L3CELL(river[0][bridge] - 1,river[1][bridge]) == 7
        && L3CELL(river[0][bridge] + 1,river[1][bridge]) == 7) {
     found = 2;
    }
    for (i = 0; i < riveramt && found != 0; i++) {
     if (found == 1
         && (river[1][bridge] - 1 == river[1][i] || river[1][bridge] + 1 == river[1][i])
         && river[0][bridge] == river[0][i]) {
      found = 0;
     }
     if (found == 2
         && (river[0][bridge] - 1 == river[0][i] || river[0][bridge] + 1 == river[0][i])
         && river[1][bridge] == river[1][i]) {
      found = 0;
     }
    }
   }
   if (found != 0) {
    if (found == 1) {
     river[2][bridge] = 44;
    } else {
     river[2][bridge] = 45;
    }
    rivercnt++;
    for (bridge = 0; bridge <= riveramt; bridge++) {
     L3CELL(river[0][bridge],river[1][bridge]) = river[2][bridge];
    }
   } else {
    bail = FALSE;
   }
  }
 }
}
static BOOL DRLG_L3SpawnEdge(int x, int y, int *totarea)
{
 BYTE i;
 static BYTE spawntable[15] = { 0x00, 0x0A, 0x43, 0x05, 0x2c, 0x06, 0x09, 0x00, 0x00, 0x1c, 0x83, 0x06, 0x09, 0x0A, 0x05 };
 if (*totarea > 40) {
  return TRUE;
 }
 if (x < 0 || y < 0 || x >= DMAXX || y >= DMAXY) {
  return TRUE;
 }
 if (L3CELL(x,y) & 0x80) {
  return FALSE;
 }
 if (L3CELL(x,y) > 15) {
  return TRUE;
 }
 i = L3CELL(x,y);
 L3CELL(x,y) |= 0x80;
 *totarea += 1;
 if (spawntable[i] & 8 && DRLG_L3SpawnEdge(x, y - 1, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 4 && DRLG_L3SpawnEdge(x, y + 1, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 2 && DRLG_L3SpawnEdge(x + 1, y, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 1 && DRLG_L3SpawnEdge(x - 1, y, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 0x80 && DRLG_L3Spawn(x, y - 1, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 0x40 && DRLG_L3Spawn(x, y + 1, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 0x20 && DRLG_L3Spawn(x + 1, y, totarea) == TRUE) {
  return TRUE;
 }
 if (spawntable[i] & 0x10 && DRLG_L3Spawn(x - 1, y, totarea) == TRUE) {
  return TRUE;
 }
 return FALSE;
}
static BOOL DRLG_L3Spawn(int x, int y, int *totarea)
{
 BYTE i;
 static BYTE spawntable[15] = { 0x00, 0x0A, 0x03, 0x05, 0x0C, 0x06, 0x09, 0x00, 0x00, 0x0C, 0x03, 0x06, 0x09, 0x0A, 0x05 };
 if (*totarea > 40) {
  return TRUE;
 }
 if (x < 0 || y < 0 || x >= DMAXX || y >= DMAXY) {
  return TRUE;
 }
 if (L3CELL(x,y) & 0x80) {
  return FALSE;
 }
 if (L3CELL(x,y) > 15) {
  return TRUE;
 }
 i = L3CELL(x,y);
 L3CELL(x,y) |= 0x80;
 *totarea += 1;
 if (i != 8) {
  if (spawntable[i] & 8 && DRLG_L3SpawnEdge(x, y - 1, totarea) == TRUE) {
   return TRUE;
  }
  if (spawntable[i] & 4 && DRLG_L3SpawnEdge(x, y + 1, totarea) == TRUE) {
   return TRUE;
  }
  if (spawntable[i] & 2 && DRLG_L3SpawnEdge(x + 1, y, totarea) == TRUE) {
   return TRUE;
  }
  if (spawntable[i] & 1 && DRLG_L3SpawnEdge(x - 1, y, totarea) == TRUE) {
   return TRUE;
  }
 } else {
  if (DRLG_L3Spawn(x + 1, y, totarea) == TRUE) {
   return TRUE;
  }
  if (DRLG_L3Spawn(x - 1, y, totarea) == TRUE) {
   return TRUE;
  }
  if (DRLG_L3Spawn(x, y + 1, totarea) == TRUE) {
   return TRUE;
  }
  if (DRLG_L3Spawn(x, y - 1, totarea) == TRUE) {
   return TRUE;
  }
 }
 return FALSE;
}
static void DRLG_L3Pool()
{
 int i, j, dunx, duny, totarea, poolchance;
 BOOL found;
 BYTE k;
 static BYTE poolsub[15] = { 0, 35, 26, 36, 25, 29, 34, 7, 33, 28, 27, 37, 32, 31, 30 };
 for (duny = 0; duny < DMAXY; duny++) {
  for (dunx = 0; dunx < DMAXY; dunx++) {
   if (L3CELL(dunx,duny) != 8) {
    continue;
   }
   L3CELL(dunx,duny) |= 0x80;
   totarea = 1;
   if (dunx + 1 < DMAXX) {
    found = DRLG_L3Spawn(dunx + 1, duny, &totarea);
   } else {
    found = TRUE;
   }
   if (dunx - 1 > 0 && !found) {
    found = DRLG_L3Spawn(dunx - 1, duny, &totarea);
   } else {
    found = TRUE;
   }
   if (duny + 1 < DMAXY && !found) {
    found = DRLG_L3Spawn(dunx, duny + 1, &totarea);
   } else {
    found = TRUE;
   }
   if (duny - 1 > 0 && !found) {
    found = DRLG_L3Spawn(dunx, duny - 1, &totarea);
   } else {
    found = TRUE;
   }
   poolchance = random_(0, 100);
   for (j = duny - totarea; j < duny + totarea; j++) {
    for (i = dunx - totarea; i < dunx + totarea; i++) {
     if (j >= 0 && j < DMAXY && i >= 0 && i < DMAXX && (L3CELL(i,j) & 0x80)) {
      L3CELL(i,j) &= ~0x80;
      if (totarea > 4 && poolchance < 25 && !found) {
       k = poolsub[L3CELL(i,j)];
       if (k != 0 && k <= 37) {
        L3CELL(i,j) = k;
       }
       lavapool = TRUE;
      }
     }
    }
   }
  }
 }
}
static void DRLG_L3PoolFix()
{
 int dunx, duny;
 for (duny = 0; duny < DMAXY; duny++) {
  for (dunx = 0; dunx < DMAXX; dunx++) {
   if (L3CELL(dunx,duny) == 8) {
    if (L3CELL(dunx - 1,duny - 1) >= 25 && L3CELL(dunx - 1,duny - 1) <= 41
        && L3CELL(dunx - 1,duny) >= 25 && L3CELL(dunx - 1,duny) <= 41
        && L3CELL(dunx - 1,duny + 1) >= 25 && L3CELL(dunx - 1,duny + 1) <= 41
        && L3CELL(dunx,duny - 1) >= 25 && L3CELL(dunx,duny - 1) <= 41
        && L3CELL(dunx,duny + 1) >= 25 && L3CELL(dunx,duny + 1) <= 41
        && L3CELL(dunx + 1,duny - 1) >= 25 && L3CELL(dunx + 1,duny - 1) <= 41
        && L3CELL(dunx + 1,duny) >= 25 && L3CELL(dunx + 1,duny) <= 41
        && L3CELL(dunx + 1,duny + 1) >= 25 && L3CELL(dunx + 1,duny + 1) <= 41) {
     L3CELL(dunx,duny) = 33;
    }
   }
  }
 }
}
static BOOL DRLG_L3PlaceMiniSet(const BYTE *miniset, int tmin, int tmax, int cx, int cy, BOOL setview, int ldir)
{
 int sx, sy, sw, sh, xx, yy, i, ii, numt, trys;
 BOOL found;
 sw = miniset[0];
 sh = miniset[1];
 if (tmax - tmin == 0) {
  numt = 1;
 } else {
  numt = random_(0, tmax - tmin) + tmin;
 }
 for (i = 0; i < numt; i++) {
  sx = random_(0, DMAXX - sw);
  sy = random_(0, DMAXY - sh);
  found = FALSE;
  trys = 0;
  while (!found && trys < 200) {
   trys++;
   found = TRUE;
   if (cx != -1 && sx >= cx - sw && sx <= cx + 12) {
    sx = random_(0, DMAXX - sw);
    sy = random_(0, DMAXY - sh);
    found = FALSE;
   }
   if (cy != -1 && sy >= cy - sh && sy <= cy + 12) {
    sx = random_(0, DMAXX - sw);
    sy = random_(0, DMAXY - sh);
    found = FALSE;
   }
   ii = 2;
   for (yy = 0; yy < sh && found == TRUE; yy++) {
    for (xx = 0; xx < sw && found == TRUE; xx++) {
     if (miniset[ii] != 0 && L3CELL(xx + sx,yy + sy) != miniset[ii]) {
      found = FALSE;
     }
     if (dflags[xx + sx][yy + sy] != 0) {
      found = FALSE;
     }
     ii++;
    }
   }
   if (!found) {
    sx++;
    if (sx == DMAXX - sw) {
     sx = 0;
     sy++;
     if (sy == DMAXY - sh) {
      sy = 0;
     }
    }
   }
  }
  if (trys >= 200) {
   return TRUE;
  }
  ii = sw * sh + 2;
  for (yy = 0; yy < sh; yy++) {
   for (xx = 0; xx < sw; xx++) {
    if (miniset[ii] != 0) {
     L3CELL(xx + sx,yy + sy) = miniset[ii];
    }
    ii++;
   }
  }
 }
 if (setview == TRUE) {
  ViewX = 2 * sx + 17;
  ViewY = 2 * sy + 19;
 }
 if (ldir == 0) {
  LvlViewX = 2 * sx + 17;
  LvlViewY = 2 * sy + 19;
 }
 /* L3 returns FALSE on success, unlike L2/L4. Record only the miniset
  * installed in this successful layout, never a failed search candidate. */
 if(ldir==0){ws->up_x=sx*2+3;ws->up_y=sy*2+3;}
 if(ldir==1){ws->down_x=sx*2+3;ws->down_y=sy*2+3;}
 return FALSE;
}
static void DRLG_L3PlaceRndSet(const BYTE *miniset, int rndper)
{
 int sx, sy, sw, sh, xx, yy, ii, kk;
 BOOL found;
 sw = miniset[0];
 sh = miniset[1];
 for (sy = 0; sy < DMAXX - sh; sy++) {
  for (sx = 0; sx < DMAXY - sw; sx++) {
   found = TRUE;
   ii = 2;
   for (yy = 0; yy < sh && found == TRUE; yy++) {
    for (xx = 0; xx < sw && found == TRUE; xx++) {
     if (miniset[ii] != 0 && L3CELL(xx + sx,yy + sy) != miniset[ii]) {
      found = FALSE;
     }
     if (dflags[xx + sx][yy + sy] != 0) {
      found = FALSE;
     }
     ii++;
    }
   }
   kk = sw * sh + 2;
   if (miniset[kk] >= 84 && miniset[kk] <= 100 && found == TRUE) {
    if (L3CELL(sx - 1,sy) >= 84 && L3CELL(sx - 1,sy) <= 100) {
     found = FALSE;
    }
    if (L3CELL(sx + 1,sy) >= 84 && L3CELL(sx - 1,sy) <= 100) {
     found = FALSE;
    }
    if (L3CELL(sx,sy + 1) >= 84 && L3CELL(sx - 1,sy) <= 100) {
     found = FALSE;
    }
    if (L3CELL(sx,sy - 1) >= 84 && L3CELL(sx - 1,sy) <= 100) {
     found = FALSE;
    }
   }
   if (found == TRUE && random_(0, 100) < rndper) {
    for (yy = 0; yy < sh; yy++) {
     for (xx = 0; xx < sw; xx++) {
      if (miniset[kk] != 0) {
       L3CELL(xx + sx,yy + sy) = miniset[kk];
      }
      kk++;
     }
    }
   }
  }
 }
}
static BOOL WoodVertU(int i, int y)
{
 if ((L3CELL(i + 1,y) > 152 || L3CELL(i + 1,y) < 130)
     && (L3CELL(i - 1,y) > 152 || L3CELL(i - 1,y) < 130)) {
  if (L3CELL(i,y) == 7) {
   return TRUE;
  }
  if (L3CELL(i,y) == 10) {
   return TRUE;
  }
  if (L3CELL(i,y) == 126) {
   return TRUE;
  }
  if (L3CELL(i,y) == 129) {
   return TRUE;
  }
  if (L3CELL(i,y) == 134) {
   return TRUE;
  }
  if (L3CELL(i,y) == 136) {
   return TRUE;
  }
 }
 return FALSE;
}
static BOOL WoodVertD(int i, int y)
{
 if ((L3CELL(i + 1,y) > 152 || L3CELL(i + 1,y) < 130)
     && (L3CELL(i - 1,y) > 152 || L3CELL(i - 1,y) < 130)) {
  if (L3CELL(i,y) == 7) {
   return TRUE;
  }
  if (L3CELL(i,y) == 2) {
   return TRUE;
  }
  if (L3CELL(i,y) == 134) {
   return TRUE;
  }
  if (L3CELL(i,y) == 136) {
   return TRUE;
  }
 }
 return FALSE;
}
static BOOL WoodHorizL(int x, int j)
{
 if ((L3CELL(x,j + 1) > 152 || L3CELL(x,j + 1) < 130)
     && (L3CELL(x,j - 1) > 152 || L3CELL(x,j - 1) < 130)) {
  if (L3CELL(x,j) == 7) {
   return TRUE;
  }
  if (L3CELL(x,j) == 9) {
   return TRUE;
  }
  if (L3CELL(x,j) == 121) {
   return TRUE;
  }
  if (L3CELL(x,j) == 124) {
   return TRUE;
  }
  if (L3CELL(x,j) == 135) {
   return TRUE;
  }
  if (L3CELL(x,j) == 137) {
   return TRUE;
  }
 }
 return FALSE;
}
static BOOL WoodHorizR(int x, int j)
{
 if ((L3CELL(x,j + 1) > 152 || L3CELL(x,j + 1) < 130)
     && (L3CELL(x,j - 1) > 152 || L3CELL(x,j - 1) < 130)) {
  if (L3CELL(x,j) == 7) {
   return TRUE;
  }
  if (L3CELL(x,j) == 4) {
   return TRUE;
  }
  if (L3CELL(x,j) == 135) {
   return TRUE;
  }
  if (L3CELL(x,j) == 137) {
   return TRUE;
  }
 }
 return FALSE;
}
void AddFenceDoors()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 7) {
    if (L3CELL(i - 1,j) <= 152 && L3CELL(i - 1,j) >= 130
        && L3CELL(i + 1,j) <= 152 && L3CELL(i + 1,j) >= 130) {
     L3CELL(i,j) = 146;
     continue;
    }
   }
   if (L3CELL(i,j) == 7) {
    if (L3CELL(i,j - 1) <= 152 && L3CELL(i,j - 1) >= 130
        && L3CELL(i,j + 1) <= 152 && L3CELL(i,j + 1) >= 130) {
     L3CELL(i,j) = 147;
     continue;
    }
   }
  }
 }
}
void FenceDoorFix()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 146) {
    if (L3CELL(i + 1,j) > 152 || L3CELL(i + 1,j) < 130
        || L3CELL(i - 1,j) > 152 || L3CELL(i - 1,j) < 130) {
     L3CELL(i,j) = 7;
     continue;
    }
   }
   if (L3CELL(i,j) == 146) {
    if (L3CELL(i + 1,j) != 130 && L3CELL(i - 1,j) != 130
        && L3CELL(i + 1,j) != 132 && L3CELL(i - 1,j) != 132
        && L3CELL(i + 1,j) != 133 && L3CELL(i - 1,j) != 133
        && L3CELL(i + 1,j) != 134 && L3CELL(i - 1,j) != 134
        && L3CELL(i + 1,j) != 136 && L3CELL(i - 1,j) != 136
        && L3CELL(i + 1,j) != 138 && L3CELL(i - 1,j) != 138
        && L3CELL(i + 1,j) != 140 && L3CELL(i - 1,j) != 140) {
     L3CELL(i,j) = 7;
     continue;
    }
   }
   if (L3CELL(i,j) == 147) {
    if (L3CELL(i,j + 1) > 152 || L3CELL(i,j + 1) < 130
        || L3CELL(i,j - 1) > 152 || L3CELL(i,j - 1) < 130) {
     L3CELL(i,j) = 7;
     continue;
    }
   }
   if (L3CELL(i,j) == 147) {
    if (L3CELL(i,j + 1) != 131 && L3CELL(i,j - 1) != 131
        && L3CELL(i,j + 1) != 132 && L3CELL(i,j - 1) != 132
        && L3CELL(i,j + 1) != 133 && L3CELL(i,j - 1) != 133
        && L3CELL(i,j + 1) != 135 && L3CELL(i,j - 1) != 135
        && L3CELL(i,j + 1) != 137 && L3CELL(i,j - 1) != 137
        && L3CELL(i,j + 1) != 138 && L3CELL(i,j - 1) != 138
        && L3CELL(i,j + 1) != 139 && L3CELL(i,j - 1) != 139) {
     L3CELL(i,j) = 7;
     continue;
    }
   }
  }
 }
}
static void DRLG_L3Wood()
{
 int i, j, x, y, xx, yy, rt, rp, x1, y1, x2, y2;
 BOOL skip;
 for (j = 0; j < DMAXY - 1; j++) {
  for (i = 0; i < DMAXX - 1; i++) {
   if (L3CELL(i,j) == 10 && random_(0, 2) != 0) {
    x = i;
    while (L3CELL(x,j) == 10) {
     x++;
    }
    x--;
    if (x - i > 0) {
     L3CELL(i,j) = 127;
     for (xx = i + 1; xx < x; xx++) {
      if (random_(0, 2) != 0) {
       L3CELL(xx,j) = 126;
      } else {
       L3CELL(xx,j) = 129;
      }
     }
     L3CELL(x,j) = 128;
    }
   }
   if (L3CELL(i,j) == 9 && random_(0, 2) != 0) {
    y = j;
    while (L3CELL(i,y) == 9) {
     y++;
    }
    y--;
    if (y - j > 0) {
     L3CELL(i,j) = 123;
     for (yy = j + 1; yy < y; yy++) {
      if (random_(0, 2) != 0) {
       L3CELL(i,yy) = 121;
      } else {
       L3CELL(i,yy) = 124;
      }
     }
     L3CELL(i,y) = 122;
    }
   }
   if (L3CELL(i,j) == 11 && L3CELL(i + 1,j) == 10 && L3CELL(i,j + 1) == 9 && random_(0, 2) != 0) {
    L3CELL(i,j) = 125;
    x = i + 1;
    while (L3CELL(x,j) == 10) {
     x++;
    }
    x--;
    for (xx = i + 1; xx < x; xx++) {
     if (random_(0, 2) != 0) {
      L3CELL(xx,j) = 126;
     } else {
      L3CELL(xx,j) = 129;
     }
    }
    L3CELL(x,j) = 128;
    y = j + 1;
    while (L3CELL(i,y) == 9) {
     y++;
    }
    y--;
    for (yy = j + 1; yy < y; yy++) {
     if (random_(0, 2) != 0) {
      L3CELL(i,yy) = 121;
     } else {
      L3CELL(i,yy) = 124;
     }
    }
    L3CELL(i,y) = 122;
   }
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 7 && random_(0, 1) == 0 && SkipThemeRoom(i, j)) {
    rt = random_(0, 2);
    if (rt == 0) {
     y1 = j;
     while (WoodVertU(i, y1)) {
      y1--;
     }
     y1++;
     y2 = j;
     while (WoodVertD(i, y2)) {
      y2++;
     }
     y2--;
     skip = TRUE;
     if (L3CELL(i,y1) == 7) {
      skip = FALSE;
     }
     if (L3CELL(i,y2) == 7) {
      skip = FALSE;
     }
     if (y2 - y1 > 1 && skip) {
      rp = random_(0, y2 - y1 - 1) + y1 + 1;
      for (y = y1; y <= y2; y++) {
       if (y == rp) {
        continue;
       }
       if (L3CELL(i,y) == 7) {
        if (random_(0, 2) != 0) {
         L3CELL(i,y) = 135;
        } else {
         L3CELL(i,y) = 137;
        }
       }
       if (L3CELL(i,y) == 10) {
        L3CELL(i,y) = 131;
       }
       if (L3CELL(i,y) == 126) {
        L3CELL(i,y) = 133;
       }
       if (L3CELL(i,y) == 129) {
        L3CELL(i,y) = 133;
       }
       if (L3CELL(i,y) == 2) {
        L3CELL(i,y) = 139;
       }
       if (L3CELL(i,y) == 134) {
        L3CELL(i,y) = 138;
       }
       if (L3CELL(i,y) == 136) {
        L3CELL(i,y) = 138;
       }
      }
     }
    }
    if (rt == 1) {
     x1 = i;
     while (WoodHorizL(x1, j)) {
      x1--;
     }
     x1++;
     x2 = i;
     while (WoodHorizR(x2, j)) {
      x2++;
     }
     x2--;
     skip = TRUE;
     if (L3CELL(x1,j) == 7) {
      skip = FALSE;
     }
     if (L3CELL(x2,j) == 7) {
      skip = FALSE;
     }
     if (x2 - x1 > 1 && skip) {
      rp = random_(0, x2 - x1 - 1) + x1 + 1;
      for (x = x1; x <= x2; x++) {
       if (x == rp) {
        continue;
       }
       if (L3CELL(x,j) == 7) {
        if (random_(0, 2) != 0) {
         L3CELL(x,j) = 134;
        } else {
         L3CELL(x,j) = 136;
        }
       }
       if (L3CELL(x,j) == 9) {
        L3CELL(x,j) = 130;
       }
       if (L3CELL(x,j) == 121) {
        L3CELL(x,j) = 132;
       }
       if (L3CELL(x,j) == 124) {
        L3CELL(x,j) = 132;
       }
       if (L3CELL(x,j) == 4) {
        L3CELL(x,j) = 140;
       }
       if (L3CELL(x,j) == 135) {
        L3CELL(x,j) = 138;
       }
       if (L3CELL(x,j) == 137) {
        L3CELL(x,j) = 138;
       }
      }
     }
    }
   }
  }
 }
 AddFenceDoors();
 FenceDoorFix();
}
BOOL DRLG_L3Anvil()
{
 int sx, sy, sw, sh, xx, yy, ii, trys;
 BOOL found;
 sw = L3ANVIL[0];
 sh = L3ANVIL[1];
 sx = random_(0, DMAXX - sw);
 sy = random_(0, DMAXY - sh);
 found = FALSE;
 trys = 0;
 while (!found && trys < 200) {
  trys++;
  found = TRUE;
  ii = 2;
  for (yy = 0; yy < sh && found == TRUE; yy++) {
   for (xx = 0; xx < sw && found == TRUE; xx++) {
    if (L3ANVIL[ii] != 0 && L3CELL(xx + sx,yy + sy) != L3ANVIL[ii]) {
     found = FALSE;
    }
    if (dflags[xx + sx][yy + sy] != 0) {
     found = FALSE;
    }
    ii++;
   }
  }
  if (!found) {
   sx++;
   if (sx == DMAXX - sw) {
    sx = 0;
    sy++;
    if (sy == DMAXY - sh) {
     sy = 0;
    }
   }
  }
 }
 if (trys >= 200) {
  return TRUE;
 }
 ii = sw * sh + 2;
 for (yy = 0; yy < sh; yy++) {
  for (xx = 0; xx < sw; xx++) {
   if (L3ANVIL[ii] != 0) {
    L3CELL(xx + sx,yy + sy) = L3ANVIL[ii];
   }
   dflags[xx + sx][yy + sy] |= DLRG_PROTECTED;
   ii++;
  }
 }
 setpc_x = sx;
 setpc_y = sy;
 setpc_w = sw;
 setpc_h = sh;
 return FALSE;
}
void FixL3Warp()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 125 && L3CELL(i + 1,j) == 125 && L3CELL(i,j + 1) == 125 && L3CELL(i + 1,j + 1) == 125) {
    L3CELL(i,j) = 156;
    L3CELL(i + 1,j) = 155;
    L3CELL(i,j + 1) = 153;
    L3CELL(i + 1,j + 1) = 154;
    return;
   }
   if (L3CELL(i,j) == 5 && L3CELL(i + 1,j + 1) == 7) {
    L3CELL(i,j) = 7;
   }
  }
 }
}
void FixL3HallofHeroes()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 5 && L3CELL(i + 1,j + 1) == 7) {
    L3CELL(i,j) = 7;
   }
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) == 5 && L3CELL(i + 1,j + 1) == 12 && L3CELL(i + 1,j) == 7) {
    L3CELL(i,j) = 7;
    L3CELL(i,j + 1) = 7;
    L3CELL(i + 1,j + 1) = 7;
   }
   if (L3CELL(i,j) == 5 && L3CELL(i + 1,j + 1) == 12 && L3CELL(i,j + 1) == 7) {
    L3CELL(i,j) = 7;
    L3CELL(i + 1,j) = 7;
    L3CELL(i + 1,j + 1) = 7;
   }
  }
 }
}
/* Original recursive connectivity fill converted to a bounded queue for the
 * GBA's 8 KiB stack. It consumes no RNG and preserves the visited count. */
void DRLG_L3LockRec(int x,int y){
 uint16_t *queue=(void*)(dungeon_workspace+20000);unsigned head=0,tail=0;
 if(x<0||y<0||x>=40||y>=40||!lockout[x][y])return;
 queue[tail++]=y*40+x;lockout[x][y]=0;
 while(head<tail){unsigned cell=queue[head++];x=cell%40;y=cell/40;lockoutcnt++;
  const int dx[4]={0,0,-1,1},dy[4]={-1,1,0,0};
  for(int d=0;d<4;d++){int xx=x+dx[d],yy=y+dy[d];if(xx>=0&&yy>=0&&xx<40&&yy<40&&lockout[xx][yy]){lockout[xx][yy]=0;queue[tail++]=yy*40+xx;}}
 }
}

BOOL DRLG_L3Lockout()
{
 int i, j, t, fx, fy;
 t = 0;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (L3CELL(i,j) != 0) {
    lockout[i][j] = TRUE;
    fx = i;
    fy = j;
    t++;
   } else {
    lockout[i][j] = FALSE;
   }
  }
 }
 lockoutcnt = 0;
 DRLG_L3LockRec(fx, fy);
 return t == lockoutcnt;
}
static void DRLG_L3(int entry)
{
 int x1, y1, x2, y2, i, j;
 BOOL found, genok;
 unsigned all_attempts=0;uint32_t initial=ws->seed;
 lavapool = FALSE;
 do {
  do {
   do {
    world_seed_candidate(&ws->seed,currlevel,QuestStatus(Q_ANVIL)?1:0,initial,&all_attempts);
    L3_COUNT(0);L3_SEED();InitL3Dungeon();
    x1 = random_(0, 20) + 10;
    y1 = random_(0, 20) + 10;
    x2 = x1 + 2;
    y2 = y1 + 2;
    DRLG_L3FillRoom(x1, y1, x2, y2);
    DRLG_L3CreateBlock(x1, y1, 2, 0);
    DRLG_L3CreateBlock(x2, y1, 2, 1);
    DRLG_L3CreateBlock(x1, y2, 2, 2);
    DRLG_L3CreateBlock(x1, y1, 2, 3);
    if (QuestStatus(Q_ANVIL)) {
     x1 = random_(0, 10) + 10;
     y1 = random_(0, 10) + 10;
     x2 = x1 + 12;
     y2 = y1 + 12;
     DRLG_L3FloorArea(x1, y1, x2, y2);
    }
    DRLG_L3FillDiags();
    DRLG_L3FillSingles();
    DRLG_L3FillStraights();
    DRLG_L3FillDiags();
    DRLG_L3Edges();
    if (DRLG_L3GetFloorArea() >= 600) {
     found = DRLG_L3Lockout();if(!found)L3_COUNT(2);
    } else {
     L3_COUNT(1);found = FALSE;
    }
   } while (!found);
   DRLG_L3MakeMegas();
   if (entry == ENTRY_MAIN) {
     genok = DRLG_L3PlaceMiniSet(L3UP, 1, 1, -1, -1, TRUE, 0);
    if (!genok) {
      genok = DRLG_L3PlaceMiniSet(L3DOWN, 1, 1, -1, -1, FALSE, 1);
     if (!genok && currlevel == 9) {
      genok = DRLG_L3PlaceMiniSet(L3HOLDWARP, 1, 1, -1, -1, FALSE, 6);
     }
    }
   } else if (entry == ENTRY_PREV) {
     genok = DRLG_L3PlaceMiniSet(L3UP, 1, 1, -1, -1, FALSE, 0);
    if (!genok) {
      genok = DRLG_L3PlaceMiniSet(L3DOWN, 1, 1, -1, -1, TRUE, 1);
      ViewX += 2;
      ViewY -= 2;
     if (!genok && currlevel == 9) {
      genok = DRLG_L3PlaceMiniSet(L3HOLDWARP, 1, 1, -1, -1, FALSE, 6);
     }
    }
   } else {
     genok = DRLG_L3PlaceMiniSet(L3UP, 1, 1, -1, -1, FALSE, 0);
    if (!genok) {
      genok = DRLG_L3PlaceMiniSet(L3DOWN, 1, 1, -1, -1, FALSE, 1);
     if (!genok && currlevel == 9) {
      genok = DRLG_L3PlaceMiniSet(L3HOLDWARP, 1, 1, -1, -1, TRUE, 6);
     }
    }
   }
   if(genok)L3_COUNT(3);
   if (!genok && QuestStatus(Q_ANVIL)) {
    genok = DRLG_L3Anvil();if(genok)L3_COUNT(4);
   }
  } while (genok == TRUE);
   DRLG_L3Pool();if(!lavapool)L3_COUNT(5);
 } while (!lavapool);
  DRLG_L3PoolFix();
  FixL3Warp();
  DRLG_L3PlaceRndSet(L3ISLE1, 70);
  DRLG_L3PlaceRndSet(L3ISLE2, 70);
  DRLG_L3PlaceRndSet(L3ISLE3, 30);
  DRLG_L3PlaceRndSet(L3ISLE4, 30);
  DRLG_L3PlaceRndSet(L3ISLE1, 100);
  DRLG_L3PlaceRndSet(L3ISLE2, 100);
  DRLG_L3PlaceRndSet(L3ISLE5, 90);
  FixL3HallofHeroes();
  DRLG_L3River();
 if (QuestStatus(Q_ANVIL)) {
  L3CELL(setpc_x + 7,setpc_y + 5) = 7;
  L3CELL(setpc_x + 8,setpc_y + 5) = 7;
  L3CELL(setpc_x + 9,setpc_y + 5) = 7;
  if (L3CELL(setpc_x + 10,setpc_y + 5) == 17 || L3CELL(setpc_x + 10,setpc_y + 5) == 18) {
   L3CELL(setpc_x + 10,setpc_y + 5) = 45;
  }
 }
  DRLG_PlaceThemeRooms(5, 10, 7, 0, 0);
  DRLG_L3Wood();
  DRLG_L3PlaceRndSet(L3TITE1, 10);
  DRLG_L3PlaceRndSet(L3TITE2, 10);
  DRLG_L3PlaceRndSet(L3TITE3, 10);
  DRLG_L3PlaceRndSet(L3TITE6, 20);
  DRLG_L3PlaceRndSet(L3TITE7, 20);
  DRLG_L3PlaceRndSet(L3TITE8, 20);
  DRLG_L3PlaceRndSet(L3TITE9, 20);
  DRLG_L3PlaceRndSet(L3TITE10, 20);
  DRLG_L3PlaceRndSet(L3TITE11, 30);
  DRLG_L3PlaceRndSet(L3TITE12, 20);
  DRLG_L3PlaceRndSet(L3TITE13, 20);
  DRLG_L3PlaceRndSet(L3CREV1, 30);
  DRLG_L3PlaceRndSet(L3CREV2, 30);
  DRLG_L3PlaceRndSet(L3CREV3, 30);
  DRLG_L3PlaceRndSet(L3CREV4, 30);
  DRLG_L3PlaceRndSet(L3CREV5, 30);
  DRLG_L3PlaceRndSet(L3CREV6, 30);
  DRLG_L3PlaceRndSet(L3CREV7, 30);
  DRLG_L3PlaceRndSet(L3CREV8, 30);
  DRLG_L3PlaceRndSet(L3CREV9, 30);
  DRLG_L3PlaceRndSet(L3CREV10, 30);
  DRLG_L3PlaceRndSet(L3CREV11, 30);
  DRLG_L3PlaceRndSet(L3XTRA1, 25);
  DRLG_L3PlaceRndSet(L3XTRA2, 25);
  DRLG_L3PlaceRndSet(L3XTRA3, 25);
  DRLG_L3PlaceRndSet(L3XTRA4, 25);
  DRLG_L3PlaceRndSet(L3XTRA5, 25);
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   pdungeon[i][j] = L3CELL(i,j);
  }
 }
 DRLG_Init_Globals();
}
void worldgen_l3(void) { memset(local_state,0,sizeof(*local_state)); DRLG_L3(ENTRY_MAIN); }
