#ifndef WALK_BATCH_H
#define WALK_BATCH_H
#include "game.h"
/* Read-only queries share one prop pass, not a retained world cache. This
 * also handles duplicate anchors and freshly changed doors/quests correctly.
 * Coordinates are packed only after the caller has checked map bounds. */
static __attribute__((noinline)) int walk_batch_clear_filtered(const Game *g,const uint16_t *cells,unsigned count,unsigned ignore_containers){
 if(!count)return 1;
 int minx=MAP_W,miny=MAP_H,maxx=0,maxy=0;
 for(unsigned i=0;i<count;i++){
  unsigned x=cells[i]&255,y=cells[i]>>8;
  if(g->area){if(!game_walkable(g,x,y))return 0;continue;}
  if(x>=MAP_W||y>=MAP_H||!g->map[y][x])return 0;
  if((int)x<minx)minx=x;if((int)x>maxx)maxx=x;
  if((int)y<miny)miny=y;if((int)y>maxy)maxy=y;
 }
 if(g->area)return 1;
 for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&g->props[i];
  if(ignore_containers&&game_prop_is_target_container(p->kind))continue;
  if(!p->kind||p->x<minx||p->x>maxx+1||p->y<miny||p->y>maxy+1)continue;
  for(unsigned j=0;j<count;j++){
   int x=cells[j]&255,y=cells[j]>>8;
   if((unsigned)((int)p->x-x)<=1u&&(unsigned)((int)p->y-y)<=1u&&game_prop_blocks(p,x,y))return 0;
  }
 }
 return 1;
}
static inline int walk_batch_clear(const Game *g,const uint16_t *cells,unsigned count){return walk_batch_clear_filtered(g,cells,count,0);}
#endif
