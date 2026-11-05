/* Modified GBA adaptation of Devilution Source/drlg_l4.cpp.
 * Reference revision: 9f01757243412bb154d02d9af65c804159e6e0ea.
 * Original reference bytes verified; see docs/upstream-comparison.json.
 * Changes include bounded generation state, GBA RNG/instrumentation and map access.
 * Retains Sustainable Use License; see LICENSES/Devilution-LICENSE.md and NOTICE.md.
 * Function comparison is lexical evidence, not a claim of semantic equivalence.
 */
#include "shim.h"
/* Native RNG sequence; inline constant bounds for ARM speed optimization. */
static inline int l4_random(int index,int v){
 (void)index;if(v<=0)return 0;GEN_WORK();ws->seed=ws->seed*0x015a4e35u+1;
 uint32_t n=ws->seed;if(n&0x80000000u)n=0u-n;
 return (int)((v<65535?n>>16:n)%(unsigned)v);
}
#define random_ l4_random

















static const BYTE L4ConvTbl[16] = { 30, 6, 1, 6, 2, 6, 6, 6, 9, 6, 1, 6, 2, 6, 3, 6 };
static const BYTE L4USTAIRS[] = {
 4, 5,
  6, 6, 6, 6,
  6, 6, 6, 6,
  6, 6, 6, 6,
  6, 6, 6, 6,
  6, 6, 6, 6,
  0, 0, 0, 0,
 36, 38, 35, 0,
 37, 34, 33, 32,
  0, 0, 31, 0,
  0, 0, 0, 0,
};
static const BYTE L4TWARP[] = {
 4, 5,
 6, 6, 6, 6,
 6, 6, 6, 6,
 6, 6, 6, 6,
 6, 6, 6, 6,
 6, 6, 6, 6,
   0, 0, 0, 0,
 134, 136, 133, 0,
 135, 132, 131, 130,
   0, 0, 129, 0,
   0, 0, 0, 0,
};
static const BYTE L4DSTAIRS[] = {
 5, 5,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 0, 0, 0, 0, 0,
 0, 0, 45, 41, 0,
 0, 44, 43, 40, 0,
 0, 46, 42, 39, 0,
 0, 0, 0, 0, 0,
};
static const BYTE L4PENTA[] = {
 5, 5,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 0, 0, 0, 0, 0,
 0, 98, 100, 103, 0,
 0, 99, 102, 105, 0,
 0, 101, 104, 106, 0,
 0, 0, 0, 0, 0,
};
static const BYTE L4PENTA2[] = {
 5, 5,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6,
 0, 0, 0, 0, 0,
 0, 107, 109, 112, 0,
 0, 108, 111, 114, 0,
 0, 110, 113, 115, 0,
 0, 0, 0, 0, 0,
};
static const BYTE L4BTYPES[140] = {
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
 10, 11, 12, 13, 14, 15, 16, 17, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 6,
 6, 6, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 1, 2, 1, 2, 1, 2, 1, 1, 2,
 2, 0, 0, 0, 0, 0, 0, 15, 16, 9,
 12, 4, 5, 7, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

struct LocalState { int diabquad1x; int diabquad1y; int diabquad2x; int diabquad2y; int diabquad3x; int diabquad3y; int diabquad4x; int diabquad4y; BOOL hallok[20]; int l4holdx; int l4holdy; int SP4x1; int SP4y1; int SP4x2; int SP4y2; BYTE L4dungeon[80][80]; BYTE dung[20][20]; };
#define local_state ((struct LocalState *)(dungeon_workspace+40000))
#define diabquad1x (local_state->diabquad1x)
#define diabquad1y (local_state->diabquad1y)
#define diabquad2x (local_state->diabquad2x)
#define diabquad2y (local_state->diabquad2y)
#define diabquad3x (local_state->diabquad3x)
#define diabquad3y (local_state->diabquad3y)
#define diabquad4x (local_state->diabquad4x)
#define diabquad4y (local_state->diabquad4y)
#define hallok (local_state->hallok)
#define l4holdx (local_state->l4holdx)
#define l4holdy (local_state->l4holdy)
#define SP4x1 (local_state->SP4x1)
#define SP4y1 (local_state->SP4y1)
#define SP4x2 (local_state->SP4x2)
#define SP4y2 (local_state->SP4y2)
#define L4dungeon (local_state->L4dungeon)
#define dung (local_state->dung)
static void DRLG_L4Shadows();
static void InitL4Dungeon();
void DRLG_L4SetSPRoom(int rx1, int ry1);
static void L4makeDmt();
static int L4HWallOk(int i, int j);
static int L4VWallOk(int i, int j);
static void L4HorizWall(int i, int j, int dx);
static void L4VertWall(int i, int j, int dy);
static void L4AddWall();
static void L4tileFix();
static void DRLG_L4Subs();
static void L4makeDungeon();
static void uShape();
static long GetArea();
static void L4drawRoom(int x, int y, int width, int height);
static BOOL L4checkRoom(int x, int y, int width, int height);
static void L4roomGen(int x, int y, int w, int h, int dir);
static void L4firstRoom();
void L4SaveQuads();
void DRLG_L4SetRoom(BYTE *set_piece, int rx1, int ry1);
void DRLG_LoadDiabQuads(BOOL preflag);
static BOOL DRLG_L4PlaceMiniSet(const BYTE *miniset, int tmin, int tmax, int cx, int cy, BOOL setview, int ldir);
static void DRLG_L4Corners();
void L4FixRim();
void DRLG_L4GeneralFix();
static void DRLG_L4(int entry);
static void DRLG_L4Shadows()
{
 int x, y;
 BOOL okflag;
 for (y = 1; y < DMAXY; y++) {
  for (x = 1; x < DMAXY; x++) {
   okflag = FALSE;
   if (dungeon[x][y] == 3) {
    okflag = TRUE;
   }
   if (dungeon[x][y] == 4) {
    okflag = TRUE;
   }
   if (dungeon[x][y] == 8) {
    okflag = TRUE;
   }
   if (dungeon[x][y] == 15) {
    okflag = TRUE;
   }
   if (!okflag) {
    continue;
   }
   if (dungeon[x - 1][y] == 6) {
    dungeon[x - 1][y] = 47;
   }
   if (dungeon[x - 1][y - 1] == 6) {
    dungeon[x - 1][y - 1] = 48;
   }
  }
 }
}
static void InitL4Dungeon()
{
 int i, j;
 memset(dung, 0, sizeof(dung));
 memset(L4dungeon, 0, sizeof(L4dungeon));
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   dungeon[i][j] = 30;
   dflags[i][j] = 0;
  }
 }
}
void DRLG_L4SetSPRoom(int rx1, int ry1)
{
 int rw, rh, i, j;
 BYTE *sp;
 rw = pSetPiece[0];
 rh = pSetPiece[2];
 setpc_x = rx1;
 setpc_y = ry1;
 setpc_w = rw;
 setpc_h = rh;
 sp = &pSetPiece[4];
 for (j = 0; j < rh; j++) {
  for (i = 0; i < rw; i++) {
   if (*sp != 0) {
    dungeon[i + rx1][j + ry1] = *sp;
    dflags[i + rx1][j + ry1] |= DLRG_PROTECTED;
   } else {
    dungeon[i + rx1][j + ry1] = 6;
   }
   sp += 2;
  }
 }
}
static void L4makeDmt()
{
 int i, j, idx, val, dmtx, dmty;
 for (j = 0, dmty = 1; dmty <= 77; j++, dmty += 2) {
  for (i = 0, dmtx = 1; dmtx <= 77; i++, dmtx += 2) {
   val = 8 * L4dungeon[dmtx + 1][dmty + 1]
       + 4 * L4dungeon[dmtx][dmty + 1]
       + 2 * L4dungeon[dmtx + 1][dmty]
       + L4dungeon[dmtx][dmty];
   idx = L4ConvTbl[val];
   dungeon[i][j] = idx;
  }
 }
}
static int L4HWallOk(int i, int j)
{
 int x;
 BOOL wallok;
 for (x = 1; dungeon[i + x][j] == 6; x++) {
  if (dflags[i + x][j] != 0) {
   break;
  }
  if (dungeon[i + x][j - 1] != 6) {
   break;
  }
  if (dungeon[i + x][j + 1] != 6) {
   break;
  }
 }
 wallok = FALSE;
 if (dungeon[i + x][j] == 10) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 12) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 13) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 15) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 16) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 21) {
  wallok = TRUE;
 }
 if (dungeon[i + x][j] == 22) {
  wallok = TRUE;
 }
 if (x <= 3) {
  wallok = FALSE;
 }
 if (wallok) {
  return x;
 } else {
  return -1;
 }
}
static int L4VWallOk(int i, int j)
{
 int y;
 BOOL wallok;
 for (y = 1; dungeon[i][j + y] == 6; y++) {
  if (dflags[i][j + y] != 0) {
   break;
  }
  if (dungeon[i - 1][j + y] != 6) {
   break;
  }
  if (dungeon[i + 1][j + y] != 6) {
   break;
  }
 }
 wallok = FALSE;
 if (dungeon[i][j + y] == 8) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 9) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 11) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 14) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 15) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 16) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 21) {
  wallok = TRUE;
 }
 if (dungeon[i][j + y] == 23) {
  wallok = TRUE;
 }
 if (y <= 3) {
  wallok = FALSE;
 }
 if (wallok) {
  return y;
 } else {
  return -1;
 }
}
static void L4HorizWall(int i, int j, int dx)
{
 int xx;
 if (dungeon[i][j] == 13) {
  dungeon[i][j] = 17;
 }
 if (dungeon[i][j] == 16) {
  dungeon[i][j] = 11;
 }
 if (dungeon[i][j] == 12) {
  dungeon[i][j] = 14;
 }
 for (xx = 1; xx < dx; xx++) {
  dungeon[i + xx][j] = 2;
 }
 if (dungeon[i + dx][j] == 15) {
  dungeon[i + dx][j] = 14;
 }
 if (dungeon[i + dx][j] == 10) {
  dungeon[i + dx][j] = 17;
 }
 if (dungeon[i + dx][j] == 21) {
  dungeon[i + dx][j] = 23;
 }
 if (dungeon[i + dx][j] == 22) {
  dungeon[i + dx][j] = 29;
 }
 xx = random_(0, dx - 3) + 1;
 dungeon[i + xx][j] = 57;
 dungeon[i + xx + 2][j] = 56;
 dungeon[i + xx + 1][j] = 60;
 if (dungeon[i + xx][j - 1] == 6) {
  dungeon[i + xx][j - 1] = 58;
 }
 if (dungeon[i + xx + 1][j - 1] == 6) {
  dungeon[i + xx + 1][j - 1] = 59;
 }
}
static void L4VertWall(int i, int j, int dy)
{
 int yy;
 if (dungeon[i][j] == 14) {
  dungeon[i][j] = 17;
 }
 if (dungeon[i][j] == 8) {
  dungeon[i][j] = 9;
 }
 if (dungeon[i][j] == 15) {
  dungeon[i][j] = 10;
 }
 for (yy = 1; yy < dy; yy++) {
  dungeon[i][j + yy] = 1;
 }
 if (dungeon[i][j + dy] == 11) {
  dungeon[i][j + dy] = 17;
 }
 if (dungeon[i][j + dy] == 9) {
  dungeon[i][j + dy] = 10;
 }
 if (dungeon[i][j + dy] == 16) {
  dungeon[i][j + dy] = 13;
 }
 if (dungeon[i][j + dy] == 21) {
  dungeon[i][j + dy] = 22;
 }
 if (dungeon[i][j + dy] == 23) {
  dungeon[i][j + dy] = 29;
 }
 yy = random_(0, dy - 3) + 1;
 dungeon[i][j + yy] = 53;
 dungeon[i][j + yy + 2] = 52;
 dungeon[i][j + yy + 1] = 6;
 if (dungeon[i - 1][j + yy] == 6) {
  dungeon[i - 1][j + yy] = 54;
 }
 if (dungeon[i - 1][j + yy - 1] == 6) {
  dungeon[i - 1][j + yy - 1] = 55;
 }
}
static void L4AddWall()
{
 int i, j, x, y;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dflags[i][j] != 0) {
    continue;
   }
   if (dungeon[i][j] == 10 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 12 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 13 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 15 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 16 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 21 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 22 && random_(0, 100) < 100) {
    x = L4HWallOk(i, j);
    if (x != -1) {
     L4HorizWall(i, j, x);
    }
   }
   if (dungeon[i][j] == 8 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 9 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 11 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 14 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 15 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 16 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 21 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
   if (dungeon[i][j] == 23 && random_(0, 100) < 100) {
    y = L4VWallOk(i, j);
    if (y != -1) {
     L4VertWall(i, j, y);
    }
   }
  }
 }
}
static void L4tileFix()
{
 int i, j;
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 6)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 13;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 14;
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 6)
    dungeon[i + 1][j] = 2;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 9 && dungeon[i + 1][j] == 6)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 13;
   if (dungeon[i][j] == 6 && dungeon[i + 1][j] == 14)
    dungeon[i + 1][j] = 15;
   if (dungeon[i][j] == 6 && dungeon[i][j + 1] == 13)
    dungeon[i][j + 1] = 16;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 6 && dungeon[i][j - 1] == 1)
    dungeon[i][j - 1] = 1;
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 27;
   if (dungeon[i][j] == 27 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 27;
   if (dungeon[i][j] == 27 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 27)
    dungeon[i + 1][j] = 26;
   if (dungeon[i][j] == 27 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 15)
    dungeon[i + 1][j] = 14;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 15)
    dungeon[i + 1][j] = 14;
   if (dungeon[i][j] == 22 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 27 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 6 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] != 0)
    dungeon[i + 1][j] = 22;
   if (dungeon[i][j] == 22 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 1 && dungeon[i + 1][j - 1] == 1)
    dungeon[i + 1][j] = 13;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 30 && dungeon[i][j + 1] == 6)
    dungeon[i + 1][j] = 28;
   if (dungeon[i][j] == 16 && dungeon[i + 1][j] == 6 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 27;
   if (dungeon[i][j] == 16 && dungeon[i][j + 1] == 30 && dungeon[i + 1][j + 1] == 30)
    dungeon[i][j + 1] = 27;
   if (dungeon[i][j] == 6 && dungeon[i + 1][j] == 30 && dungeon[i + 1][j - 1] == 6)
    dungeon[i + 1][j] = 21;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] == 9)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 9 && dungeon[i + 1][j] == 15)
    dungeon[i + 1][j] = 14;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] == 2)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 18)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 9 && dungeon[i + 1][j] == 15)
    dungeon[i + 1][j] = 14;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 19 && dungeon[i + 1][j - 1] == 30)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 24 && dungeon[i][j - 1] == 30 && dungeon[i][j - 2] == 6)
    dungeon[i][j - 1] = 21;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 28;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 28;
   if (dungeon[i][j] == 28 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 28 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 19 && dungeon[i + 2][j] == 2 && dungeon[i + 1][j - 1] == 18 && dungeon[i + 1][j + 1] == 1)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 19 && dungeon[i + 2][j] == 2 && dungeon[i + 1][j - 1] == 22 && dungeon[i + 1][j + 1] == 1)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 19 && dungeon[i + 2][j] == 2 && dungeon[i + 1][j - 1] == 18 && dungeon[i + 1][j + 1] == 13)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 21 && dungeon[i + 2][j] == 2 && dungeon[i + 1][j - 1] == 18 && dungeon[i + 1][j + 1] == 1)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j + 1] == 1 && dungeon[i + 1][j - 1] == 22 && dungeon[i + 2][j] == 3)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 28 && dungeon[i + 2][j] == 30 && dungeon[i + 1][j - 1] == 6)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 28 && dungeon[i + 2][j] == 1)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] == 30)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 28 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j - 1] == 21)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] == 30)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 18)
    dungeon[i + 1][j] = 25;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 9 && dungeon[i + 2][j] == 2)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 10)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 15 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 22 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 18 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 24 && dungeon[i - 1][j] == 30)
    dungeon[i - 1][j] = 19;
   if (dungeon[i][j] == 21 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 21 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 22 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 21 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 16 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 22 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 18 && dungeon[i + 2][j] == 30)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 9 && dungeon[i + 1][j + 1] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 27 && dungeon[i + 1][j + 1] == 2)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 23 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 23 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 25 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 22 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 23 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 11 && dungeon[i + 1][j] == 15)
    dungeon[i + 1][j] = 14;
   if (dungeon[i][j] == 23 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 27)
    dungeon[i + 1][j] = 26;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 18)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 26 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 29 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 29 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 1 && dungeon[i][j - 1] == 15)
    dungeon[i][j - 1] = 10;
   if (dungeon[i][j] == 18 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 23 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 18 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 30 && dungeon[i + 1][j + 1] == 30)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 28 && dungeon[i + 1][j - 1] == 6)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 23 && dungeon[i + 1][j] == 18 && dungeon[i][j - 1] == 6)
    dungeon[i + 1][j] = 24;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 23 && dungeon[i + 2][j] == 30)
    dungeon[i + 1][j] = 28;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 28 && dungeon[i + 2][j] == 30 && dungeon[i + 1][j - 1] == 6)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 23 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 29 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 29 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 26 && dungeon[i + 1][j] == 30)
    dungeon[i + 1][j] = 19;
   if (dungeon[i][j] == 16 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 25 && dungeon[i][j + 1] == 30)
    dungeon[i][j + 1] = 18;
   if (dungeon[i][j] == 18 && dungeon[i][j + 1] == 2)
    dungeon[i][j + 1] = 15;
   if (dungeon[i][j] == 11 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 13;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 13 && dungeon[i + 1][j - 1] == 6)
    dungeon[i + 1][j] = 16;
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 21 && dungeon[i][j + 1] == 24 && dungeon[i][j + 2] == 1)
    dungeon[i][j + 1] = 17;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j + 1] == 9 && dungeon[i + 1][j - 1] == 1 && dungeon[i + 2][j] == 16)
    dungeon[i + 1][j] = 29;
   if (dungeon[i][j] == 2 && dungeon[i - 1][j] == 6)
    dungeon[i - 1][j] = 8;
   if (dungeon[i][j] == 1 && dungeon[i][j - 1] == 6)
    dungeon[i][j - 1] = 7;
   if (dungeon[i][j] == 6 && dungeon[i + 1][j] == 15 && dungeon[i + 1][j + 1] == 4)
    dungeon[i + 1][j] = 10;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 6)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 9 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 10 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 16)
    dungeon[i][j + 1] = 13;
   if (dungeon[i][j] == 6 && dungeon[i][j + 1] == 13)
    dungeon[i][j + 1] = 16;
   if (dungeon[i][j] == 25 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 28 && dungeon[i][j - 1] == 6 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 10)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 11 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 10 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 27 && dungeon[i + 1][j] == 9)
    dungeon[i + 1][j] = 11;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 1)
    dungeon[i + 1][j] = 16;
   if (dungeon[i][j] == 11 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 9 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 14 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 3)
    dungeon[i + 1][j] = 5;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 5 && dungeon[i + 1][j - 1] == 16)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 9 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 1 && dungeon[i][j - 1] == 8)
    dungeon[i][j - 1] = 9;
   if (dungeon[i][j] == 28 && dungeon[i + 1][j] == 23 && dungeon[i + 1][j + 1] == 3)
    dungeon[i + 1][j] = 16;
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 10)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 17 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 10 && dungeon[i + 1][j] == 4)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 17 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 29 && dungeon[i][j + 1] == 9)
    dungeon[i][j + 1] = 10;
   if (dungeon[i][j] == 13 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 9 && dungeon[i][j + 1] == 16)
    dungeon[i][j + 1] = 13;
   if (dungeon[i][j] == 10 && dungeon[i][j + 1] == 16)
    dungeon[i][j + 1] = 13;
   if (dungeon[i][j] == 16 && dungeon[i][j + 1] == 3)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 11 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 10 && dungeon[i + 1][j] == 3 && dungeon[i + 1][j - 1] == 16)
    dungeon[i + 1][j] = 12;
   if (dungeon[i][j] == 16 && dungeon[i][j + 1] == 5)
    dungeon[i][j + 1] = 12;
   if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 6)
    dungeon[i][j + 1] = 4;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j] == 13 && dungeon[i][j + 1] == 10)
    dungeon[i + 1][j + 1] = 12;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 10)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 22 && dungeon[i][j + 1] == 11)
    dungeon[i][j + 1] = 17;
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 28 && dungeon[i + 2][j] == 16)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 28 && dungeon[i + 1][j] == 23 && dungeon[i + 1][j + 1] == 1 && dungeon[i + 2][j] == 6)
    dungeon[i + 1][j] = 16;
  }
 }
 for (j = 0; j < DMAXY; j++) {
  for (i = 0; i < DMAXX; i++) {
   if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 28 && dungeon[i + 2][j] == 16)
    dungeon[i + 1][j] = 23;
   if (dungeon[i][j] == 21 && dungeon[i + 1][j - 1] == 21 && dungeon[i + 1][j + 1] == 13 && dungeon[i + 2][j] == 2)
    dungeon[i + 1][j] = 17;
   if (dungeon[i][j] == 19 && dungeon[i + 1][j] == 15 && dungeon[i + 1][j + 1] == 12)
    dungeon[i + 1][j] = 17;
  }
 }
}
static void DRLG_L4Subs()
{
 int x, y, i, rv;
 BYTE c;
 for (y = 0; y < DMAXY; y++) {
  for (x = 0; x < DMAXX; x++) {
   rv = random_(0, 3);
   if (rv == 0) {
    c = dungeon[x][y];
    c = L4BTYPES[c];
    if (c != 0 && dflags[x][y] == 0) {
     rv = random_(0, 16);
     i = -1;
     while (rv >= 0) {
      i++;
      if (i == sizeof(L4BTYPES)) {
       i = 0;
      }
      if (c == L4BTYPES[i]) {
       rv--;
      }
     }
     dungeon[x][y] = i;
    }
   }
  }
 }
 for (y = 0; y < DMAXY; y++) {
  for (x = 0; x < DMAXX; x++) {
   rv = random_(0, 10);
   if (rv == 0) {
    c = dungeon[x][y];
    if (L4BTYPES[c] == 6 && dflags[x][y] == 0) {
     dungeon[x][y] = random_(0, 3) + 95;
    }
   }
  }
 }
}
static void L4makeDungeon()
{
 int i, j, k, l;
 for (j = 0; j < 20; j++) {
  for (i = 0; i < 20; i++) {
   k = i << 1;
   l = j << 1;
   L4dungeon[k][l] = dung[i][j];
   L4dungeon[k][l + 1] = dung[i][j];
   L4dungeon[k + 1][l] = dung[i][j];
   L4dungeon[k + 1][l + 1] = dung[i][j];
  }
 }
 for (j = 0; j < 20; j++) {
  for (i = 0; i < 20; i++) {
   k = i << 1;
   l = j << 1;
   L4dungeon[k][l + 40] = dung[i][19 - j];
   L4dungeon[k][l + 41] = dung[i][19 - j];
   L4dungeon[k + 1][l + 40] = dung[i][19 - j];
   L4dungeon[k + 1][l + 41] = dung[i][19 - j];
  }
 }
 for (j = 0; j < 20; j++) {
  for (i = 0; i < 20; i++) {
   k = i << 1;
   l = j << 1;
   L4dungeon[k + 40][l] = dung[19 - i][j];
   L4dungeon[k + 40][l + 1] = dung[19 - i][j];
   L4dungeon[k + 41][l] = dung[19 - i][j];
   L4dungeon[k + 41][l + 1] = dung[19 - i][j];
  }
 }
 for (j = 0; j < 20; j++) {
  for (i = 0; i < 20; i++) {
   k = i << 1;
   l = j << 1;
   L4dungeon[k + 40][l + 40] = dung[19 - i][19 - j];
   L4dungeon[k + 40][l + 41] = dung[19 - i][19 - j];
   L4dungeon[k + 41][l + 40] = dung[19 - i][19 - j];
   L4dungeon[k + 41][l + 41] = dung[19 - i][19 - j];
  }
 }
}
static void uShape()
{
 int j, i, rv;
 for (j = 19; j >= 0; j--) {
  for (i = 19; i >= 0; i--) {
   if (dung[i][j] != 1) {
    hallok[j] = FALSE;
   }
   if (dung[i][j] == 1) {
    if (dung[i][j + 1] == 1 && dung[i + 1][j + 1] == 0) {
     hallok[j] = TRUE;
    } else {
     hallok[j] = FALSE;
    }
    i = 0;
   }
  }
 }
 rv = random_(0, 19) + 1;
 do {
  if (hallok[rv]) {
   for (i = 19; i >= 0; i--) {
    if (dung[i][rv] == 1) {
     i = -1;
     rv = 0;
    } else {
     dung[i][rv] = 1;
     dung[i][rv + 1] = 1;
    }
   }
  } else {
   rv++;
   if (rv == 20) {
    rv = 1;
   }
  }
 } while (rv != 0);
 for (i = 19; i >= 0; i--) {
  for (j = 19; j >= 0; j--) {
   if (dung[i][j] != 1) {
    hallok[i] = FALSE;
   }
   if (dung[i][j] == 1) {
    if (dung[i + 1][j] == 1 && dung[i + 1][j + 1] == 0) {
     hallok[i] = TRUE;
    } else {
     hallok[i] = FALSE;
    }
    j = 0;
   }
  }
 }
 rv = random_(0, 19) + 1;
 do {
  if (hallok[rv]) {
   for (j = 19; j >= 0; j--) {
    if (dung[rv][j] == 1) {
     j = -1;
     rv = 0;
    } else {
     dung[rv][j] = 1;
     dung[rv + 1][j] = 1;
    }
   }
  } else {
   rv++;
   if (rv == 20) {
    rv = 1;
   }
  }
 } while (rv != 0);
}
static long GetArea()
{
 int i, j;
 long rv;
 rv = 0;
 for (j = 0; j < 20; j++) {
  for (i = 0; i < 20; i++) {
   if (dung[i][j] == 1) {
    rv++;
   }
  }
 }
 return rv;
}
static void L4drawRoom(int x, int y, int width, int height)
{
 int i, j;
 for (j = 0; j < height; j++) {
  for (i = 0; i < width; i++) {
   dung[i + x][j + y] = 1;
  }
 }
}
static BOOL L4checkRoom(int x, int y, int width, int height)
{
 int i, j;
 if (x <= 0 || y <= 0) {
  return FALSE;
 }
 for (j = 0; j < height; j++) {
  for (i = 0; i < width; i++) {
   if (i + x < 0 || i + x >= 20 || j + y < 0 || j + y >= 20) {
    return FALSE;
   }
   if (dung[i + x][j + y] != 0) {
    return FALSE;
   }
  }
 }
 return TRUE;
}
/* Every queued task owns an already drawn, disjoint room of at least2x2
 * cells in the20x20 grid. At most100 tasks can exist, including the root.
 * The phase ends before later conversion, room IDs or renderer work. */
typedef struct { int16_t x,y,w,h,dir; } L4RoomTask;
enum { L4_ROOM_TASK_OFFSET=48000,L4_ROOM_TASK_CAPACITY=128 };
_Static_assert(sizeof(L4RoomTask)==10,"L4 room task layout");
_Static_assert(40000+sizeof(struct LocalState)<=L4_ROOM_TASK_OFFSET,"L4 local state before DFS arena");
_Static_assert(L4_ROOM_TASK_OFFSET+L4_ROOM_TASK_CAPACITY*sizeof(L4RoomTask)<=63000,"L4 DFS before heap flags");
_Static_assert(20*20/4<=L4_ROOM_TASK_CAPACITY,"L4 DFS frontier capacity");
static void L4roomGen(int x, int y, int w, int h, int dir)
{
 L4RoomTask *todo=(void*)(dungeon_workspace+L4_ROOM_TASK_OFFSET);
 unsigned count=1;todo[0]=(L4RoomTask){x,y,w,h,dir};
 while(count){
  L4RoomTask task=todo[--count];x=task.x;y=task.y;w=task.w;h=task.h;dir=task.dir;
  int num;
  BOOL ran, ran2;
  int width, height, rx, ry, ry2;
  int cw, ch, cx1, cy1, cx2;
  int dirProb = random_(0, 4);
  switch (dir == 1 ? dirProb != 0 : dirProb == 0) {
  case FALSE:
   num = 0;
   do {
    cw = (random_(0, 5) + 2) & ~1;
    ch = (random_(0, 5) + 2) & ~1;
    cy1 = h / 2 + y - ch / 2;
    cx1 = x - cw;
    /* Match the room written below: retail swapped cw/ch and could write past dung. */
    ran = L4checkRoom(cx1 - 1, cy1 - 1, cw + 1, ch + 2);
    num++;
   } while (ran == FALSE && num < 20);
   if (ran == TRUE)
    L4drawRoom(cx1, cy1, cw, ch);
   cx2 = x + w;
   ran2 = L4checkRoom(cx2, cy1 - 1, cw + 1, ch + 2);
   if (ran2 == TRUE)
    L4drawRoom(cx2, cy1, cw, ch);
   /* Both rooms must be drawn before either subtree consumes more RNG. */
   if (ran2 == TRUE)
    todo[count++] = (L4RoomTask){ cx2, cy1, cw, ch, 1 };
   if (ran == TRUE)
    todo[count++] = (L4RoomTask){ cx1, cy1, cw, ch, 1 };
   break;
  case TRUE:
   num = 0;
   do {
    width = (random_(0, 5) + 2) & ~1;
    height = (random_(0, 5) + 2) & ~1;
    rx = w / 2 + x - width / 2;
    ry = y - height;
    ran = L4checkRoom(rx - 1, ry - 1, width + 2, height + 1);
    num++;
   } while (ran == FALSE && num < 20);
   if (ran == TRUE)
    L4drawRoom(rx, ry, width, height);
   ry2 = y + h;
   ran2 = L4checkRoom(rx - 1, ry2, width + 2, height + 1);
   if (ran2 == TRUE)
    L4drawRoom(rx, ry2, width, height);
   if (ran2 == TRUE)
    todo[count++] = (L4RoomTask){ rx, ry2, width, height, 0 };
   if (ran == TRUE)
    todo[count++] = (L4RoomTask){ rx, ry, width, height, 0 };
   break;
  }
 }
}
static void L4firstRoom()
{
 int x, y, w, h, rndx, rndy, xmin, xmax, ymin, ymax, tx, ty;
 if (currlevel != 16) {
  if (currlevel == quests[Q_WARLORD]._qlevel && quests[Q_WARLORD]._qactive != QUEST_NOTAVAIL) {
   w = 11;
   h = 11;
  } else if (currlevel == quests[Q_BETRAYER]._qlevel && gbMaxPlayers != 1) {
   w = 11;
   h = 11;
  } else {
   w = random_(0, 5) + 2;
   h = random_(0, 5) + 2;
  }
 } else {
  w = 14;
  h = 14;
 }
 xmin = (20 - w) >> 1;
 xmax = 19 - w;
 rndx = random_(0, xmax - xmin + 1) + xmin;
 if (rndx + w > 19) {
  tx = w + rndx - 19;
  x = rndx - tx + 1;
 } else {
  x = rndx;
 }
 ymin = (20 - h) >> 1;
 ymax = 19 - h;
 rndy = random_(0, ymax - ymin + 1) + ymin;
 if (rndy + h > 19) {
  ty = h + rndy - 19;
  y = rndy - ty + 1;
 } else {
  y = rndy;
 }
 if (currlevel == 16) {
  l4holdx = x;
  l4holdy = y;
 }
 if (QuestStatus(Q_WARLORD) || currlevel == quests[Q_BETRAYER]._qlevel && gbMaxPlayers != 1) {
  SP4x1 = x + 1;
  SP4y1 = y + 1;
  SP4x2 = SP4x1 + w;
  SP4y2 = SP4y1 + h;
 } else {
  SP4x1 = 0;
  SP4y1 = 0;
  SP4x2 = 0;
  SP4y2 = 0;
 }
 L4drawRoom(x, y, w, h);
 L4roomGen(x, y, w, h, random_(0, 2));
}
void L4SaveQuads()
{
 int i, j, x, y;
 x = l4holdx;
 y = l4holdy;
 for (j = 0; j < 14; j++) {
  for (i = 0; i < 14; i++) {
   dflags[i + x][j + y] = 1;
   dflags[DMAXX - 1 - i - x][j + y] = 1;
   dflags[i + x][DMAXY - 1 - j - y] = 1;
   dflags[DMAXX - 1 - i - x][DMAXY - 1 - j - y] = 1;
  }
 }
}
void DRLG_L4SetRoom(BYTE *set_piece, int rx1, int ry1)
{
 int rw, rh, i, j;
 BYTE *sp;
 rw = set_piece[0];
 rh = set_piece[2];
 sp = &set_piece[4];
 for (j = 0; j < rh; j++) {
  for (i = 0; i < rw; i++) {
   if (*sp != 0) {
    dungeon[i + rx1][j + ry1] = *sp;
    dflags[i + rx1][j + ry1] |= DLRG_PROTECTED;
   } else {
    dungeon[i + rx1][j + ry1] = 6;
   }
   sp += 2;
  }
 }
}
void DRLG_LoadDiabQuads(BOOL preflag)
{
 BYTE *lpSetPiece;
 lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab1.DUN", NULL);
 diabquad1x = 4 + l4holdx;
 diabquad1y = 4 + l4holdy;
 DRLG_L4SetRoom(lpSetPiece, diabquad1x, diabquad1y);
 mem_free_dbg(lpSetPiece);
 if (preflag) {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab2b.DUN", NULL);
 } else {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab2a.DUN", NULL);
 }
 diabquad2x = 27 - l4holdx;
 diabquad2y = 1 + l4holdy;
 DRLG_L4SetRoom(lpSetPiece, diabquad2x, diabquad2y);
 mem_free_dbg(lpSetPiece);
 if (preflag) {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab3b.DUN", NULL);
 } else {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab3a.DUN", NULL);
 }
 diabquad3x = 1 + l4holdx;
 diabquad3y = 27 - l4holdy;
 DRLG_L4SetRoom(lpSetPiece, diabquad3x, diabquad3y);
 mem_free_dbg(lpSetPiece);
 if (preflag) {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab4b.DUN", NULL);
 } else {
  lpSetPiece = LoadFileInMem("Levels\\L4Data\\diab4a.DUN", NULL);
 }
 diabquad4x = 28 - l4holdx;
 diabquad4y = 28 - l4holdy;
 DRLG_L4SetRoom(lpSetPiece, diabquad4x, diabquad4y);
 mem_free_dbg(lpSetPiece);
}
static BOOL DRLG_L4PlaceMiniSet(const BYTE *miniset, int tmin, int tmax, int cx, int cy, BOOL setview, int ldir)
{
 int sx, sy, sw, sh, xx, yy, i, ii, numt, bailcnt;
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
  for (bailcnt = 0; !found && bailcnt < 200; bailcnt++) {
   found = TRUE;
   if (sx >= SP4x1 && sx <= SP4x2 && sy >= SP4y1 && sy <= SP4y2) {
    found = FALSE;
   }
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
     if (miniset[ii] != 0 && dungeon[xx + sx][yy + sy] != miniset[ii]) {
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
  if (bailcnt >= 200) {
   return FALSE;
  }
  ii = sw * sh + 2;
  for (yy = 0; yy < sh; yy++) {
   for (xx = 0; xx < sw; xx++) {
    if (miniset[ii] != 0) {
     dungeon[xx + sx][yy + sy] = miniset[ii];
     dflags[xx + sx][yy + sy] |= 8;
    }
    ii++;
   }
  }
 }
 if (currlevel == 15) {
  quests[Q_BETRAYER]._qtx = sx + 1;
  quests[Q_BETRAYER]._qty = sy + 1;
 }
 if (setview == TRUE) {
  ViewX = 2 * sx + 21;
  ViewY = 2 * sy + 22;
 }
 if (ldir == 0) {
  LvlViewX = 2 * sx + 21;
  LvlViewY = 2 * sy + 22;
 }
 if(ldir==0){ws->up_x=sx*2+3;ws->up_y=sy*2+3;} if(ldir==1){ws->down_x=sx*2+3;ws->down_y=sy*2+3;} return TRUE;
}
static void DRLG_L4Corners()
{
 int i, j;
 for (j = 1; j < DMAXY - 1; j++) {
  for (i = 1; i < DMAXX - 1; i++) {
   if (dungeon[i][j] >= 18 && dungeon[i][j] <= 30) {
    if (dungeon[i + 1][j] < 18) {
     dungeon[i][j] += 98;
    } else if (dungeon[i][j + 1] < 18) {
     dungeon[i][j] += 98;
    }
   }
  }
 }
}
void L4FixRim()
{
 int i, j;
 for (i = 0; i < 20; i++) {
  dung[i][0] = 0;
 }
 for (j = 0; j < 20; j++) {
  dung[0][j] = 0;
 }
}
void DRLG_L4GeneralFix()
{
 int i, j;
 for (j = 0; j < DMAXY - 1; j++) {
  for (i = 0; i < DMAXX - 1; i++) {
   if ((dungeon[i][j] == 24 || dungeon[i][j] == 122) && dungeon[i + 1][j] == 2 && dungeon[i][j + 1] == 5) {
    dungeon[i][j] = 17;
   }
  }
 }
}
/* Native transmission fixes run before stairs and later decorations. */
static BOOL IsDURWall(char d)
{
	if (d == 25) {
		return TRUE;
	}
	if (d == 28) {
		return TRUE;
	}
	if (d == 23) {
		return TRUE;
	}

	return FALSE;
}

static BOOL IsDLLWall(char dd)
{
	if (dd == 27) {
		return TRUE;
	}
	if (dd == 26) {
		return TRUE;
	}
	if (dd == 22) {
		return TRUE;
	}

	return FALSE;
}

static void DRLG_L4TransFix(void)
{
	uint8_t (*trans)[112]=(void*)game_rooms_candidate();
	if(!trans)return;
	int i, j, xx, yy;

	yy = 16;
	for (j = 0; j < DMAXY; j++) {
		xx = 16;
		for (i = 0; i < DMAXX; i++) {
			if (IsDURWall(dungeon[i][j]) && ws->map[196+i*48+j-1] == 18) {
				trans[xx + 1][yy] = trans[xx][yy];
				trans[xx + 1][yy + 1] = trans[xx][yy];
			}
			if (IsDLLWall(dungeon[i][j]) && dungeon[i + 1][j] == 19) {
				trans[xx][yy + 1] = trans[xx][yy];
				trans[xx + 1][yy + 1] = trans[xx][yy];
			}
			if (dungeon[i][j] == 18) {
				trans[xx + 1][yy] = trans[xx][yy];
				trans[xx + 1][yy + 1] = trans[xx][yy];
			}
			if (dungeon[i][j] == 19) {
				trans[xx][yy + 1] = trans[xx][yy];
				trans[xx + 1][yy + 1] = trans[xx][yy];
			}
			if (dungeon[i][j] == 24) {
				trans[xx + 1][yy] = trans[xx][yy];
				trans[xx][yy + 1] = trans[xx][yy];
				trans[xx + 1][yy + 1] = trans[xx][yy];
			}
			if (dungeon[i][j] == 57) {
				trans[xx - 1][yy] = trans[xx][yy + 1];
				trans[xx][yy] = trans[xx][yy + 1];
			}
			if (dungeon[i][j] == 53) {
				trans[xx][yy - 1] = trans[xx + 1][yy];
				trans[xx][yy] = trans[xx + 1][yy];
			}
			xx += 2;
		}
		yy += 2;
	}
}

static void DRLG_L4(int entry)
{
 int i, j, spi, spj, ar;
 BOOL doneflag;
 unsigned attempts=0;uint32_t initial=ws->seed;
 do {
  DRLG_InitTrans();
  do {
   world_seed_candidate(&ws->seed,currlevel,QuestStatus(Q_WARLORD)?1:0,initial,&attempts);
   InitL4Dungeon();
   L4firstRoom();
   L4FixRim();
   ar = GetArea();
   if (ar >= 173) {
    uShape();
   }
  } while (ar < 173);
  L4makeDungeon();
  L4makeDmt();
  L4tileFix();
  if (currlevel == 16) {
   L4SaveQuads();
  }
  if (QuestStatus(Q_WARLORD) || currlevel == quests[Q_BETRAYER]._qlevel && gbMaxPlayers != 1) {
   for (spi = SP4x1; spi < SP4x2; spi++) {
    for (spj = SP4y1; spj < SP4y2; spj++) {
     dflags[spi][spj] = 1;
    }
   }
  }
  L4AddWall();
  game_rooms_flood(&dungeon[0][0],48,1,6);
  DRLG_L4TransFix();
  if (setloadflag) {
   DRLG_L4SetSPRoom(SP4x1, SP4y1);
  }
  if (currlevel == 16) {
   DRLG_LoadDiabQuads(TRUE);
  }
  if (QuestStatus(Q_WARLORD)) {
   if (entry == ENTRY_MAIN) {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, TRUE, 0);
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, FALSE, 6);
    }
    ViewX++;
   } else if (entry == ENTRY_PREV) {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, FALSE, 0);
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, FALSE, 6);
    }
    ViewX = 2 * setpc_x + 22;
    ViewY = 2 * setpc_y + 22;
   } else {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, FALSE, 0);
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, TRUE, 6);
    }
    ViewX++;
   }
  } else if (currlevel != 15) {
   if (entry == ENTRY_MAIN) {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, TRUE, 0);
    if (doneflag && currlevel != 16) {
     doneflag = DRLG_L4PlaceMiniSet(L4DSTAIRS, 1, 1, -1, -1, FALSE, 1);
    }
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, FALSE, 6);
    }
    ViewX++;
   } else if (entry == ENTRY_PREV) {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, FALSE, 0);
    if (doneflag && currlevel != 16) {
     doneflag = DRLG_L4PlaceMiniSet(L4DSTAIRS, 1, 1, -1, -1, TRUE, 1);
    }
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, FALSE, 6);
    }
    ViewY++;
   } else {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, FALSE, 0);
    if (doneflag && currlevel != 16) {
     doneflag = DRLG_L4PlaceMiniSet(L4DSTAIRS, 1, 1, -1, -1, FALSE, 1);
    }
    if (doneflag && currlevel == 13) {
     doneflag = DRLG_L4PlaceMiniSet(L4TWARP, 1, 1, -1, -1, TRUE, 6);
    }
    ViewX++;
   }
  } else {
   if (entry == ENTRY_MAIN) {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, TRUE, 0);
    if (doneflag) {
     if (gbMaxPlayers == 1 && quests[Q_DIABLO]._qactive != QUEST_ACTIVE) {
      doneflag = DRLG_L4PlaceMiniSet(L4PENTA, 1, 1, -1, -1, FALSE, 1);
     } else {
      doneflag = DRLG_L4PlaceMiniSet(L4PENTA2, 1, 1, -1, -1, FALSE, 1);
     }
    }
    ViewX++;
   } else {
    doneflag = DRLG_L4PlaceMiniSet(L4USTAIRS, 1, 1, -1, -1, FALSE, 0);
    if (doneflag) {
     if (gbMaxPlayers == 1 && quests[Q_DIABLO]._qactive != QUEST_ACTIVE) {
      doneflag = DRLG_L4PlaceMiniSet(L4PENTA, 1, 1, -1, -1, TRUE, 1);
     } else {
      doneflag = DRLG_L4PlaceMiniSet(L4PENTA2, 1, 1, -1, -1, TRUE, 1);
     }
    }
    ViewY++;
   }
  }
 } while (!doneflag);
 DRLG_L4GeneralFix();
 if (currlevel != 16) {
  DRLG_PlaceThemeRooms(7, 10, 6, 8, 1);
 }
 DRLG_L4Shadows();
 DRLG_L4Corners();
 DRLG_L4Subs();
 DRLG_Init_Globals();
 if (QuestStatus(Q_WARLORD)) {
  for (j = 0; j < DMAXY; j++) {
   for (i = 0; i < DMAXX; i++) {
    pdungeon[i][j] = dungeon[i][j];
   }
  }
 }
 DRLG_CheckQuests(SP4x1, SP4y1);
 if (currlevel == 15) {
  for (j = 0; j < DMAXY; j++) {
   for (i = 0; i < DMAXX; i++) {
    if (dungeon[i][j] == 98) {
     Make_SetPC(i - 1, j - 1, 5, 5);
    }
    if (dungeon[i][j] == 107) {
     Make_SetPC(i - 1, j - 1, 5, 5);
    }
   }
  }
 }
 if (currlevel == 16) {
  for (j = 0; j < DMAXY; j++) {
   for (i = 0; i < DMAXX; i++) {
    pdungeon[i][j] = dungeon[i][j];
   }
  }
  DRLG_LoadDiabQuads(FALSE);
 }
}
void worldgen_l4(void) { memset(local_state,0,sizeof(*local_state)); DRLG_L4(ENTRY_MAIN); }

void world_diablo_layout(unsigned gates,uint8_t *out,uint16_t *origins){
 (void)gates;(void)out;
 origins[0]=diabquad1x+40*diabquad1y;origins[1]=diabquad2x+40*diabquad2y;
 origins[2]=diabquad3x+40*diabquad3y;origins[3]=diabquad4x+40*diabquad4y;
}
