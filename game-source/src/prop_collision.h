#ifndef PROP_COLLISION_H
#define PROP_COLLISION_H
#include "game.h"

/* Fill the existing bounded path-search buffer without scanning all props for
 * every cell. The result is local to one read-only search: no retained cache or
 * invalidation is needed when a door, quest, region or co-op player changes.
 * 254 is an unvisited walkable cell; 255 is blocked, as used by chase_step. */
static inline void game_chase_collision_fill(const Game *g,int ox,int oy,uint8_t cells[17*17]){
 for(int y=0;y<17;y++)for(int x=0;x<17;x++){
  int wx=ox+x,wy=oy+y;
  if(g->area)cells[y*17+x]=game_walkable(g,wx,wy)?254:255;
  else cells[y*17+x]=(unsigned)wx<MAP_W&&(unsigned)wy<MAP_H&&g->map[wy][wx]?254:255;
 }
 if(g->area)return;
 for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&g->props[i];
  if(game_prop_is_large_fountain(p->kind)){
   for(int yy=(int)p->y-1;yy<=(int)p->y;yy++)for(int xx=(int)p->x-1;xx<=(int)p->x;xx++){
    unsigned x=(unsigned)(xx-ox),y=(unsigned)(yy-oy);
    if(x<17&&y<17)cells[y*17+x]=255;
   }
   continue;
  }
  int x=(int)p->x-ox,y=(int)p->y-oy;
  if((unsigned)x>=17)continue;
  if((unsigned)y<17&&game_prop_blocks(p,p->x,p->y))cells[y*17+x]=255;
  /* Check the authoritative footprint predicate, including a sarcophagus
   * whose anchor is one row below the search window. */
  if((unsigned)(y-1)<17&&game_prop_blocks(p,p->x,(int)p->y-1))cells[(y-1)*17+x]=255;
 }
}
#endif
