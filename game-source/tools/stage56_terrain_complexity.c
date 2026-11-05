#define GRAPHICS_ASSETS
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "src/game.h"
#include "generated/dungeon_art.h"
#include "generated/floor_variants.h"
#include "generated/prop_atlas.h"
#include "generated/prop_dirty.h"
#include "src/dungeon_doors.h"
static Game g;static uint16_t layout[6400];static uint8_t lookup[6400],frames[64];
static unsigned full,partial,reveal,props,tiles,depth;
static void load(const char *base,const char *name,unsigned index,void *dst,unsigned bytes){char path[512];snprintf(path,sizeof path,"%s/%s-%u.bin",base,name,index);FILE*f=fopen(path,"rb");assert(f);assert(fread(dst,1,bytes,f)==bytes);fclose(f);}
static void tile(int px,int py){
 int hx=1280+(g.x-g.y)*16,hy=72+(g.x+g.y)*8,start=(py-72)/8;if(start<0)start=0;tiles++;
 unsigned any_depth=0;
 for(int sum=start;sum<=start+11&&sum<159;sum++){
  int foot=72+sum*8,dy=py-foot+80;if(dy<0||dy>=80)continue;
  int across=px-(1280-16)+sum*16,center=across>>5,first=center-((across&31)<16);
  for(int cx=first;cx<=first+1;cx++){
   int cy=sum-cx;if(cx<0||cy<0||cx>=80||cy>=80)continue;int cell=cy*80+cx,dx=across-cx*32;
   if(dx>=0&&dx<32){unsigned variant=layout[cell];if(variant!=65535){variant=dungeon_door_variant(&g,lookup,variant,cx,cy);unsigned extra=variant>=DART_COUNT,t=(extra?variant-DART_COUNT:variant)*40+(dy>>3)*4+(dx>>3),kind=extra?floor_variant_flags[t]:dart_tile_flags[t];if(kind){unsigned z=extra?0:dart_occludes[variant];any_depth|=z;
     if(z&&foot>hy&&px+8>hx-16&&px<hx+16&&py+8>hy-40&&py<hy)reveal++;
     else if(kind==1)full++;else partial++;
   }}}
   unsigned prop=lookup[cell];int prop_y=dy-80+64;
   if(prop&&prop_y>=0&&prop_y<64&&dx>=-16&&dx<48){unsigned t=(prop_y>>3)*8+((dx+16)>>3);if(prop_atlas_tiles[frames[prop-1]][t])props++;}
  }
 }
 if(any_depth)depth++;
}
int main(int argc,char**argv){assert(argc==3);unsigned index=atoi(argv[2]);load(argv[1],"game",index,&g,sizeof g);load(argv[1],"layout",index,layout,sizeof layout);load(argv[1],"props",index,lookup,sizeof lookup);load(argv[1],"frames",index,frames,sizeof frames);
 int x=1280+(g.props[index].x-g.props[index].y)*16-32,y=72+(g.props[index].x+g.props[index].y)*8-64;
 printf("[\n");for(unsigned step=0;step<10;step++){full=partial=reveal=props=tiles=depth=0;frames[index]=PROP_BARRELEX_BASE+step;for(unsigned row=0;row<8;row++)for(unsigned col=0;col<8;col++){const PropFrameDamage*d=step?&explosive_barrel_frame_damage[step-1][step]:0;if(!step||((d->color[row>>2]>>((row&3)*8+col))&1))tile(x+col*8,y+row*8);}printf("{\"frame\":%u,\"tiles\":%u,\"opaqueTerrainLayers\":%u,\"partialTerrainLayers\":%u,\"revealLayers\":%u,\"propLayers\":%u,\"tilesWithOccluderLayers\":%u}%s\n",step,tiles,full,partial,reveal,props,depth,step==9?"":",");}puts("]");}
