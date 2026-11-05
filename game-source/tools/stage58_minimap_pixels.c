/* Host pixel oracle: actual candidate renderer against original screen raster. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#define UI_FN
static Game game;
uint8_t town_collision[1152];
static uint8_t outline[1800],revealed[1800],fog_bits[400],ref_raster[4800];
static uint32_t load_buffer[4672];
static const uint8_t *raw_base[4];
#define MAP_OUTLINE outline
#define MAP_REVEALED revealed
#define REF_RASTER ref_raster
static unsigned map_contour_builds,map_screen_fallback,map_world_fallbacks,map_unique_tiles,map_tile_overflows;
static unsigned map_visible=1,map_init,map_last_revision,map_pending,map_line_color=0x37;
static int map_last_x=-1,map_last_y=-1,map_last_area=-1;
void game_explore_export(uint8_t *out){memcpy(out,fog_bits,400);}
unsigned game_explore_revision(void){return 1;}
static inline int map_round(int n,int divisor){return n<0?-((-n+divisor/2)/divisor):(n+divisor/2)/divisor;}
static inline unsigned outer_map_tile(unsigned n){return n<174?530+n:n<238?896+n-174:n<282?266+n-238:326+n-282;}
#include "candidate-config.inc"
#include "candidate-raster.inc"
#include "reference-raster.inc"
QuestProgress quest_progress;
uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
unsigned coop_active,coop_campaign_active;
static uint8_t scratch[159744];uint8_t *dungeon_workspace=scratch;
const uint8_t asset_world_l1[1]={0},asset_world_l2[1]={0},asset_world_l3[1]={0},asset_world_l4[1]={0};
#include "world.c"
int game_quest_valor_room(int x,int y){(void)x;(void)y;return 0;}
#include "census-dungeon.inc"
static unsigned frames,fallback_frames,overflows,scenes;
static uint32_t random_state=76543;
static unsigned random_bits(void){random_state=random_state*1664525u+1013904223u;return random_state>>8;}
static void fog_set(unsigned mode,unsigned seed){
 memset(fog_bits,0,sizeof fog_bits);random_state=seed;
 if(mode==0)memset(fog_bits,255,sizeof fog_bits);
 else if(mode==1)for(unsigned i=0;i<200;i++)fog_bits[i]=(uint8_t)random_bits();
 else for(int y=0;y<40;y++)for(int x=0;x<40;x++)if((x-20)*(x-20)+(y-20)*(y-20)<144){unsigned p=y*40+x;fog_bits[p>>3]|=1u<<(p&7);}
}
static void check_frame(const char *kind,unsigned level,unsigned seed,unsigned fog,int x,int y){
 game.x=x;game.y=y;reference_automap_rasterize();
 map_last_x=-1;map_init=0;map_last_area=game.area;map_screen_fallback=0;map_pending=0;
 automap_draw();
 if(map_pending!=2){assert(!reference_automap_pack());overflows++;printf("{\"unrepresentable\":true,\"original_also_overflows\":true,\"kind\":\"%s\",\"level\":%u,\"seed\":%u,\"fog\":%u,\"x\":%d,\"y\":%d}\n",kind,level,seed,fog,x,y);return;}
 unsigned inverse[1024];for(unsigned i=0;i<1024;i++)inverse[i]=9999;
 for(unsigned i=0;i<map_unique_tiles;i++)inverse[map_tile(i)]=i;
 for(unsigned sy=0;sy<160;sy++)for(unsigned sx=0;sx<240;sx++){
  unsigned wx=map_screen_fallback?sx:(sx+MAP_WORLD_X+map_round((x-y)*320,180)-120)&511;
  unsigned wy=map_screen_fallback?sy:(sy+MAP_WORLD_Y+map_round((x+y)*320,362)-80)&255;
  unsigned tx=wx/8,ty=wy/8,entry=MAP_UPLOAD_REFS[(tx>>5)*1024+ty*32+(tx&31)];
  unsigned id=inverse[entry&1023];assert(id<map_unique_tiles);assert((entry>>12)==(map_line_color>>4));
  unsigned px=wx&7,py=wy&7;if(entry&1024)px=7-px;if(entry&2048)py=7-py;
  unsigned got=(MAP_UPLOAD_TILES[id*8+py]>>(px*4))&15;
  unsigned want=((ref_raster[(sy/8*30+sx/8)*8+(sy&7)]>>(sx&7))&1)?(map_line_color&15):0;
  if(got!=want){fprintf(stderr,"pixel mismatch %s L%u seed%u fog%u hero%d,%d fallback%u pixel%u,%u got%u want%u\n",kind,level,seed,fog,x,y,map_screen_fallback,sx,sy,got,want);exit(2);}
 }
 frames++;fallback_frames+=map_screen_fallback;
 if(map_screen_fallback)printf("{\"fallback_exact\":true,\"kind\":\"%s\",\"level\":%u,\"seed\":%u,\"fog\":%u,\"x\":%d,\"y\":%d,\"tiles\":%u}\n",kind,level,seed,fog,x,y,map_unique_tiles);
}
static void scene(const char *kind,unsigned level,unsigned seed,unsigned fog){
 fog_set(fog,seed);automap_build_outline();reference_automap_build_outline();scenes++;
 static const int positions[][2]={{0,0},{79,0},{0,79},{79,79},{40,40},{10,30},{70,30},{30,10},{30,70},{95,0},{0,95},{95,95}};
 for(unsigned i=0;i<(game.area?12:9);i++)check_frame(kind,level,seed,fog,positions[i][0],positions[i][1]);
 if(!strcmp(kind,"synthetic")){check_frame(kind,level,seed,fog,8,45);check_frame(kind,level,seed,fog,63,45);}
}
int main(int argc,char **argv){
 assert(argc==2);setbuf(stdout,0);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t town[9216];assert(fread(town,1,sizeof town,f)==sizeof town);fclose(f);town_pack_collision(town);
 game.area=1;scene("town",0,0,0);game.area=0;
 for(unsigned level=1;level<=16;level++)for(unsigned seed=1;seed<=2;seed++){
  dungeon_level=level;memset(&game,0,sizeof game);memset(&quest_progress,0,sizeof quest_progress);game_generate_dungeon(&game,seed);
  for(unsigned fog=0;fog<3;fog++)scene("native",level,seed,fog);
 }
 for(unsigned pattern=0;pattern<8;pattern++){
  memset(game.map,0,sizeof game.map);for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++)game.map[y][x]=(x/(pattern+1)+y/(pattern+1))&1;
  for(unsigned fog=0;fog<3;fog++)scene("synthetic",0,pattern,fog);
 }
 printf("{\"status\":\"pass\",\"scenes\":%u,\"pixel_exact_frames\":%u,\"fallback_frames\":%u,\"pixels_compared\":%u,\"unrepresentable_frames\":%u}\n",scenes,frames,fallback_frames,frames*38400,overflows);
 return 0;
}
