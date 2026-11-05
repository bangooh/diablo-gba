#include "game.h"
#include "../generated/dungeon_art.h"
#include "cathedral_rules.h"
#include "../generated/floor_variants.h"
static int floor_at(const Game *g,int x,int y){return x>=0&&y>=0&&x<MAP_W&&y<MAP_H&&g->map[y][x];}
static unsigned macro_index(unsigned id){for(unsigned i=0;i<sizeof(dart_macro_ids);i++)if(dart_macro_ids[i]==id)return i;return 21;}
/* Original non-Hellfire L5STAIRSUP replacements, in TIL (not MIN) units. */
static const uint8_t stairs[4][4]={{22,66,23,22},{63,64,65,2},{13,67,68,13},{13,13,13,13}};
void game_dungeon_set_macro(Game *g,int x,int y,unsigned id){
    const uint8_t *p=dart_macros[macro_index(id)];
    for(int j=0;j<2;j++)for(int i=0;i<2;i++)g->map[y*2+j][x*2+i]=(uint8_t)((dart_solid[p[j*2+i]]&&!dart_door[p[j*2+i]])?0:(id==11||id==12)?id+3:1);
}
void game_dungeon_set_stairs(Game *g){
    for(int y=0;y<4;y++)for(int x=0;x<4;x++)game_dungeon_set_macro(g,x,y,stairs[y][x]);
    g->map[2][2]=2;
}
/* Apply the original ordered seam repairs without changing saved collision.
 * Walls with the same SOL footprint can be exchanged; passage-closing rules
 * belong to the full generator, not the visual restoration of an old save. */
static unsigned mask_for(unsigned id){
    const uint8_t *p=dart_macros[macro_index(id)];unsigned bits=0;
    for(int i=0;i<4;i++)if(!dart_solid[p[i]])bits|=1u<<i;return bits;
}
void game_dungeon_fix_walls(uint8_t m[MACRO_W][MACRO_W],int legacy){
    int limit=legacy?12:MACRO_W;
    const int limits[4]={0,7,41,44};
    for(int pass=0;pass<3;pass++)for(int y=0;y<limit;y++)for(int x=0;x<limit;x++){
        for(int r=limits[pass];r<limits[pass+1];r++){
            const signed char *v=wall_rules[r];int nx=x+v[1],ny=y+v[2];
            if(nx<0||ny<0||nx>=limit||ny>=limit||(legacy&&nx<4&&ny<4))continue;
            if(m[y][x]==v[0]&&m[ny][nx]==v[3]&&(!legacy||mask_for(v[3])==mask_for(v[4])))m[ny][nx]=v[4];
        }
    }
}
/* Odd floor bytes persist architecture; 251 marks the 40x40 format.
 * The legacy 253 marker still decodes the old 9x9 plan after migration. */
int game_dungeon_read_macros(const Game *g,uint8_t m[MACRO_W][MACRO_W]){
    int n=-1,width=0;
    for(int c=0;c<MAP_W*MAP_H;c++){
        unsigned v=g->map[c/MAP_W][c%MAP_W];if(!v||v==2)continue;
        if(n<0){if(v!=251&&v!=253)return 0;width=v==251?40:9;n=0;continue;}
        unsigned id=v>>1;
        if(!(v&1)||id==0||id>sizeof(dart_macro_ids))return 0;
        id=dart_macro_ids[id-1];
        m[n/width][n%width]=(uint8_t)id;n++;
        if(n==width*width)return 1;
    }return 0;
}
int game_dungeon_commit_macros(Game *g,uint8_t m[MACRO_W][MACRO_W]){
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++)g->map[y][x]=0;
    for(int y=0;y<MACRO_W;y++)for(int x=0;x<MACRO_W;x++)game_dungeon_set_macro(g,x,y,m[y][x]);
    g->down_x=g->down_y=0;
    for(int y=0;y<40;y++)for(int x=0;x<40;x++)if(m[y][x]==59){g->down_x=x*2+1;g->down_y=y*2+1;}
    /* Stair upper-middle macro64 is the exit. */
    for(int y=0;y<MACRO_W;y++)for(int x=0;x<MACRO_W;x++)if(m[y][x]==64)g->map[y*2][x*2]=2;
    int n=-1;
    for(int c=0;c<MAP_W*MAP_H;c++){
        uint8_t *v=&g->map[c/MAP_W][c%MAP_W];if(!*v||*v==2)continue;
        if(n<0){*v=251;n=0;}else if(n<1600){*v=(uint8_t)(((macro_index(m[n/40][n%40])+1)<<1)|1);n++;}else *v=1;
    }return n==1600;
}
static void dungeon_room_macros(const Game *g,uint8_t macros[MACRO_W][MACRO_W]){
 for(int y=0;y<MACRO_W;y++)for(int x=0;x<MACRO_W;x++){
  unsigned bits=0;for(int j=0;j<2;j++)for(int i=0;i<2;i++)if(g->map[y*2+j][x*2+i])bits|=1u<<(j*2+i);
  unsigned id=bits==15?13:bits==14?15:bits==10?1:bits==12?2:bits==8?4:22;
  if(bits==14){unsigned tag=g->map[y*2+1][x*2+1];if(tag==14||tag==15)id=tag-3;}
  macros[y][x]=(uint8_t)((g->map[3][0]&&x<4&&y<4)?stairs[y][x]:id);
 }
 if(!game_dungeon_read_macros(g,macros))game_dungeon_fix_walls(macros,1);
}
#include "cathedral_rooms.inc"
_Static_assert(DART_COUNT+4*FLOOR_VARIANT_COUNT<DUNGEON_VISUAL_EMPTY,"visual atlas capacity");
void game_dungeon_visuals(const Game *g,uint16_t *tiles,uint8_t *depth){
    for(int i=0;i<MAP_W*MAP_H;i++){tiles[i]=DUNGEON_VISUAL_EMPTY;if(depth)depth[i]=0;}
    /* v0.17 maps are recoverable from their 2x2 SOL patterns; saves need no new fields.
     * The stair landing at x=0,y=3 distinguishes them from v0.13-v0.16 maps. */
    if(1){
        uint8_t (*macros)[MACRO_W]=(void*)dungeon_workspace;
        dungeon_room_macros(g,macros);
        for(int y=0;y<MACRO_W;y++)for(int x=0;x<MACRO_W;x++){
            unsigned id=macros[y][x];
            if(!(x<4&&y<4)&&!mask_for(id)){int near=0;for(int j=-1;j<=2;j++)for(int i=-1;i<=2;i++)near|=floor_at(g,x*2+i,y*2+j);if(!near)continue;}
            /* L5Subs only exchanges members of the same tile family. Keep all
             * stairs, walls and the Butcher set piece's neighbourhood intact.
             * This visual-only pass never changes collision or saved map codes. */
            unsigned variant=0;
            if(id==13){
                int protected=0;
                for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++){
                    int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=40||yy>=40)continue;
                    unsigned other=macros[yy][xx];
                    if((other>=57&&other<=68)||other>=166)protected=1;
                }
                uint32_t h=campaign_seed^(x*0x9e3779b9u)^(y*0x85ebca6bu)^(dungeon_level*0xc2b2ae35u);h^=h>>16;
                if(!protected&&!(h&3))variant=((h>>8)&15)%(FLOOR_VARIANT_COUNT+1);
            }
            if(variant){
                for(int j=0;j<2;j++)for(int i=0;i<2;i++){unsigned c=(y*2+j)*MAP_W+x*2+i;tiles[c]=DART_COUNT+(variant-1)*4+j*2+i;if(depth)depth[c]=0;}
                continue;
            }
            const uint8_t *p=dart_macros[macro_index(id)];
            for(int j=0;j<2;j++)for(int i=0;i<2;i++){unsigned c=(y*2+j)*MAP_W+x*2+i,v=p[j*2+i];tiles[c]=(uint16_t)v;if(depth)depth[c]=dart_occludes[v];}
        }
    }else{
        /* Legacy collision maps remain unchanged. Use base architectural pieces only;
         * special stairs/decorations must never be random wall substitutions. */
        for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++){
            unsigned v=DUNGEON_VISUAL_EMPTY,c=y*MAP_W+x;
            if(floor_at(g,x,y)){const uint8_t *p=dart_macros[macro_index(13)];v=p[(y&1)*2+(x&1)];}
            else {int a=floor_at(g,x+1,y),b=floor_at(g,x,y+1);
                if(a&&b&&floor_at(g,x-1,y)&&floor_at(g,x,y-1))v=DART_P150;
                else if(a&&b)v=DART_P9;
                else if(a)v=(y&1)?DART_P3:DART_P1;
                else if(b)v=(x&1)?DART_P6:DART_P5;
            }
            if(v!=DUNGEON_VISUAL_EMPTY){tiles[c]=(uint16_t)v;if(depth)depth[c]=dart_occludes[v];}
        }
    }
}
