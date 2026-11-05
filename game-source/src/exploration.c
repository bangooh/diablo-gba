#include "game.h"
/*40x40 overview cells,200 bytes per area. No Game-layout growth. */
static uint8_t explored[200];
static unsigned revision;
unsigned game_explore_revision(void){return revision;}
static int last_x=-1,last_y=-1,last_area=-1,last_radius=-1;
void game_explore_reset(void){for(int i=0;i<200;i++)explored[i]=0;last_x=last_y=last_area=last_radius=-1;revision++;}
void game_explore_export(uint8_t *out){for(int i=0;i<200;i++)out[i]=explored[i];}
void game_explore_import(const uint8_t *in){for(int i=0;i<200;i++)explored[i]=in[i];last_x=last_y=last_area=last_radius=-1;revision++;}
/* Secluded shrines reveal this floor through the same saved exploration map.
 * Bump its revision once, so an always-on minimap rebuilds only once. */
void game_explore_reveal(void){for(int i=0;i<200;i++)explored[i]=255;last_x=last_y=last_area=last_radius=-1;revision++;}
static int floor_at(const Game *g,int x,int y){return g->area?town_walkable(x,y):g->map[y][x]!=0;}
static int visible(const Game *g,int tx,int ty){
 int x=g->x,y=g->y,dx=tx-x,dy=ty-y,sx=dx<0?-1:1,sy=dy<0?-1:1;dx=dx<0?-dx:dx;dy=dy<0?-dy:dy;int err=dx-dy;
 while(x!=tx||y!=ty){int nx=x,ny=y,v=err*2;if(v>-dy){err-=dy;nx+=sx;}if(v<dx){err+=dx;ny+=sy;}
  if(nx!=x&&ny!=y&&(!floor_at(g,nx,y)||!floor_at(g,x,ny)))return 0;
  if(nx==tx&&ny==ty)return 1; // Reveal the boundary wall, not beyond it.
  if(!floor_at(g,nx,ny))return 0;x=nx;y=ny;
 }return 1;
}
int game_explored(const Game *g,int x,int y){
 int size=g->area?96:80;if(x<0||y<0||x>=size||y>=size)return 0;
 if(g->area)return 1;
 int cell=(y/2)*40+x/2;return !!(explored[cell/8]&(1u<<(cell&7)));
}
void game_explore_update(const Game *g){
 if(g->area)return; /* Tristram is public; only dungeons have discovery fog. */
 /* Keep the port's baseline7-cell overview; retail vision never falls below10. */
 int radius=7+(int)game_vision_radius()-10;
 if(last_x==g->x&&last_y==g->y&&last_area==g->area&&last_radius==radius)return;
 last_radius=radius;
 last_x=g->x;last_y=g->y;last_area=g->area;int size=80;
 for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++){
  int x=g->x+dx,y=g->y+dy;if(x<0||y<0||x>=size||y>=size)continue;
  int cell=(y/2)*40+x/2;if(explored[cell/8]&(1u<<(cell&7)))continue;
  if(visible(g,x,y)){explored[cell/8]|=1u<<(cell&7);revision++;}
 }
}
