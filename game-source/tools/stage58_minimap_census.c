/* Host-only capacity census; uses generated private copy of native contours. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#define UI_FN
static Game game;
uint8_t town_collision[1152];
static uint8_t outline[1800],revealed[1800],fog_bits[400];
static uint32_t raster[16384/4];
#define MAP_OUTLINE outline
#define MAP_REVEALED revealed
#define MAP_RASTER ((uint8_t*)raster)
static unsigned map_contour_builds;
void game_explore_export(uint8_t *out){memcpy(out,fog_bits,400);}
static inline int map_round(int n,int divisor){return n<0?-((-n+divisor/2)/divisor):(n+divisor/2)/divisor;}
#include "census-raster.inc"
QuestProgress quest_progress;
uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
unsigned coop_active,coop_campaign_active;
static uint8_t scratch[159744];uint8_t *dungeon_workspace=scratch;
const uint8_t asset_world_l1[1]={0},asset_world_l2[1]={0},asset_world_l3[1]={0},asset_world_l4[1]={0};
#include "world.c"
/* Geometry is copied verbatim up to population; NPC/monster spawning does
 * not change map collision and is outside this atlas capacity census. */
int game_quest_valor_room(int x,int y){(void)x;(void)y;return 0;}
#include "census-dungeon.inc"
static uint32_t random_state=76543;
static unsigned random_bits(void){random_state=random_state*1664525u+1013904223u;return random_state>>8;}
static uint64_t canonical[2048];
static int cmp64(const void *pa,const void *pb){uint64_t a=*(const uint64_t*)pa,b=*(const uint64_t*)pb;return (a>b)-(a<b);}
static unsigned count_tiles(void){
 for(unsigned t=0;t<2048;t++){
  uint32_t a=raster[t*2],b=raster[t*2+1],oa=a,ob=b;
  for(unsigned f=1;f<4;f++){
   uint32_t h=(f&1)?map_flip_x(oa):oa,j=(f&1)?map_flip_x(ob):ob;
   if(f&2){uint32_t old=h;h=map_flip_y(j);j=map_flip_y(old);}
   if(h<a||(h==a&&j<b)){a=h;b=j;}
  }
  canonical[t]=((uint64_t)a<<32)|b;
 }
 qsort(canonical,2048,sizeof canonical[0],cmp64);unsigned n=1;
 for(unsigned i=1;i<2048;i++)n+=canonical[i]!=canonical[i-1];
 return n;
}
static void fog_set(unsigned mode,unsigned seed){
 memset(fog_bits,0,sizeof fog_bits);random_state=seed;
 if(mode==0)memset(fog_bits,255,sizeof fog_bits);
 else if(mode==1)for(unsigned i=0;i<200;i++)fog_bits[i]=(uint8_t)random_bits();
 else if(mode==2){for(unsigned y=0;y<40;y++)for(unsigned x=0;x<40;x++)if((int)(x-20)*(int)(x-20)+(int)(y-20)*(int)(y-20)<144){unsigned p=y*40+x;fog_bits[p>>3]|=1u<<(p&7);}}
}
static void scene(const char *kind,unsigned level,unsigned seed,unsigned fog){
 fog_set(fog,seed);automap_rasterize();unsigned n=count_tiles();
 printf("{\"kind\":\"%s\",\"level\":%u,\"seed\":%u,\"fog\":%u,\"tiles\":%u}\n",kind,level,seed,fog,n);
}
int main(int argc,char **argv){
 assert(argc==2);setbuf(stdout,0);
 FILE *f=fopen(argv[1],"rb");assert(f);uint8_t town[9216];assert(fread(town,1,sizeof town,f)==sizeof town);fclose(f);
 town_pack_collision(town);game.area=1;automap_build_outline();scene("town",0,0,0);game.area=0;
 for(unsigned level=1;level<=16;level++)for(unsigned seed=1;seed<=32;seed++){
  dungeon_level=level;memset(&game,0,sizeof game);memset(&quest_progress,0,sizeof quest_progress);game_generate_dungeon(&game,seed);automap_build_outline();
  for(unsigned fog=0;fog<3;fog++)scene("native",level,seed,fog);
 }
 for(unsigned pattern=0;pattern<16;pattern++){
  memset(game.map,0,sizeof game.map);
  for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++)game.map[y][x]=pattern<8?((x/(pattern+1)+y/(pattern+1))&1):((x%(pattern+1)<pattern-4)&&(y%(pattern+1)<pattern-4));
  automap_build_outline();for(unsigned fog=0;fog<3;fog++)scene("synthetic",0,pattern,fog);
 }
 return 0;
}
