/* Doubled-map pixel oracle, scratch/VRAM canaries and recenter lifecycle. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#define UI_FN
static Game game;
uint8_t town_collision[1152];
static uint8_t outline[1800],revealed[1800],fog_bits[400],ref_raster[65536];
static struct {uint32_t before[8],buffer[4672],after[8];} scratch_guard;
#define load_buffer scratch_guard.buffer
static const uint8_t *raw_base[4];
#define MAP_OUTLINE outline
#define MAP_REVEALED revealed
#define REF_RASTER ref_raster
static unsigned map_contour_builds,map_world_fallbacks,map_unique_tiles,map_tile_overflows;
static uint8_t map_screen_fallback,map_screen_live;
static unsigned map_visible=1,map_init,map_last_revision,map_pending,map_line_color=0x37,revision=1;
static int map_last_x=-1,map_last_y=-1,map_last_area=-1,map_ref_x,map_ref_y,cam_x,cam_y;
static uint32_t io[256/4],vram[65536/4];
#define R16(a) (*(uint16_t*)((uint8_t*)io+(a)-0x04000000))
static void dma(const void *src,void *dst,unsigned count){unsigned at=(uintptr_t)dst-0x06000000;assert(at+count<=65536);memcpy((uint8_t*)vram+at,src,count);}
void game_explore_export(uint8_t *out){memcpy(out,fog_bits,400);}
unsigned game_explore_revision(void){return revision;}
static inline unsigned outer_map_tile(unsigned n){return n<174?530+n:n<238?896+n-174:n<282?266+n-238:326+n-282;}
#include "candidate-config.inc"
#include "automap_raster.inc"
#include "reference-raster.inc"
QuestProgress quest_progress;
uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
unsigned coop_active,coop_campaign_active;
static uint8_t scratch[159744];uint8_t *dungeon_workspace=scratch;
const uint8_t asset_world_l1[1]={0},asset_world_l2[1]={0},asset_world_l3[1]={0},asset_world_l4[1]={0};
#include "world.c"
int game_quest_valor_room(int x,int y){(void)x;(void)y;return 0;}
#include "census-dungeon.inc"
static unsigned frames,fallback_frames,overflows,scenes,max_native_tiles,max_synthetic_tiles,native_fallbacks,reuses,recenters,scene_fog,fallback_fog[4],native_overflows;
static uint32_t random_state=76543;
static unsigned random_bits(void){random_state=random_state*1664525u+1013904223u;return random_state>>8;}
static void fog_set(unsigned mode,unsigned seed){
 memset(fog_bits,0,sizeof fog_bits);random_state=seed;revision++;
 if(mode==0)memset(fog_bits,255,sizeof fog_bits);
 else if(mode==1)for(unsigned i=0;i<200;i++)fog_bits[i]=(uint8_t)random_bits();
 else if(mode==2)for(int y=0;y<40;y++)for(int x=0;x<40;x++)if((x-20)*(x-20)+(y-20)*(y-20)<144){unsigned p=y*40+x;fog_bits[p>>3]|=1u<<(p&7);}
}
static void set_camera(int x,int y){game.x=x;game.y=y;cam_x=(game.area?1536:DUNGEON_ORIGIN)+(x-y)*16-120;cam_y=(game.area?128:72)+(x+y)*8-84;map_ref_x=map_round((x-y)*640,180);map_ref_y=map_round((x+y)*640,360);}
static void check_canary(void){for(unsigned i=0;i<8;i++)assert(scratch_guard.before[i]==0xa5a5a5a5&&scratch_guard.after[i]==0xa5a5a5a5);}
static void check_pixels(const char *kind){
 memset(vram,0xcc,sizeof vram);automap_upload();map_screen_live=map_screen_fallback;automap_scroll();
 const uint8_t *pixels=(const uint8_t*)vram;uint8_t allowed[65536]={0};
 for(unsigned i=0;i<map_unique_tiles;i++)memset(allowed+0x8000+map_tile(i)*32,1,32);
 memset(allowed+0xe800,1,(map_screen_live&1)?2048:4096);
 for(unsigned i=0;i<65536;i++)if(!allowed[i])assert(pixels[i]==0xcc);
 for(unsigned sy=0;sy<160;sy++)for(unsigned sx=0;sx<240;sx++){
  unsigned wx=(sx+R16(0x04000018))&((map_screen_live&1)?255:511),wy=(sy+R16(0x0400001a))&255;
  unsigned tx=wx/8,ty=wy/8,entry=((uint16_t*)vram)[0xe800/2+(tx>>5)*1024+ty*32+(tx&31)];
  assert((entry>>12)==(map_line_color>>4));
  unsigned px=wx&7,py=wy&7;if(entry&1024)px=7-px;if(entry&2048)py=7-py;
  unsigned byte=pixels[0x8000+(entry&1023)*32+py*4+px/2],got=(byte>>((px&1)*4))&15;
  int rx=(int)sx+512+map_ref_x-120,ry=(int)sy+64+map_ref_y-80;
  unsigned want=(unsigned)rx<1024&&(unsigned)ry<512?((ref_raster[(ry/8*128+rx/8)*8+(ry&7)]>>(rx&7))&1)?(map_line_color&15):0:0;
  if(got!=want){fprintf(stderr,"pixel mismatch %s hero%d,%d fallback%u pixel%u,%u got%u want%u\n",kind,game.x,game.y,map_screen_live,sx,sy,got,want);exit(2);}
 }
 check_canary();frames++;fallback_frames+=map_screen_fallback&1;
 if(strcmp(kind,"synthetic")){native_fallbacks+=map_screen_fallback&1;fallback_fog[scene_fog]+=map_screen_fallback&1;if(map_unique_tiles>max_native_tiles)max_native_tiles=map_unique_tiles;}
 else if(map_unique_tiles>max_synthetic_tiles)max_synthetic_tiles=map_unique_tiles;
}
static void check_frame(const char *kind,int x,int y){
 set_camera(x,y);map_last_x=-1;map_init=0;map_last_area=game.area;map_screen_fallback=0;map_pending=0;automap_draw();
 if(map_pending!=2){assert(map_init&&map_screen_fallback==1);check_canary();overflows++;if(strcmp(kind,"synthetic"))native_overflows++;printf("{\"overflow\":\"%s\",\"x\":%d,\"y\":%d,\"level\":%u}\n",kind,x,y,dungeon_level);return;}check_pixels(kind);
 if(!(map_screen_fallback&1)){
  int ox=map_last_x,oy=map_last_y;map_pending=0;set_camera(x+1,y);automap_draw();assert(!map_pending&&map_last_x==ox&&map_last_y==oy);reuses++;check_pixels(kind);
  set_camera(x+20,y+20);automap_draw();assert(map_pending==2);recenters++;if(map_pending==2)check_pixels(kind);
 }
}
static void scene(const char *kind,unsigned seed,unsigned fog){
 scene_fog=fog;fog_set(fog,seed);automap_build_outline();reference_automap_rasterize();scenes++;
 static const int positions[][2]={{0,0},{79,0},{0,79},{79,79},{40,40},{10,30},{70,30},{30,10},{30,70},{95,0},{0,95},{95,95}};
 for(unsigned i=0;i<(game.area?12:9);i++)check_frame(kind,positions[i][0],positions[i][1]);
 if(!strcmp(kind,"synthetic")){check_frame(kind,8,45);check_frame(kind,63,45);}
}
static void check_junctions(void){
 /* Raster isolated diamonds at all97x97 grid phases, then independently
  * flood their pixels. Four source edges must make one closed eight-connected
  * contour with the expected pixel perimeter and four shared vertices. */
 game.area=1;map_screen_fallback=0;
 for(int y=0;y<96;y++)for(int x=0;x<96;x++){
  game.x=x;game.y=y;memset(MAP_RASTER,0,16384);
  map_line(x,y,x+1,y);map_line(x+1,y,x+1,y+1);map_line(x+1,y+1,x,y+1);map_line(x,y+1,x,y);
  unsigned queue[128],head=0,tail=0,count=0;uint8_t seen[32][32]={0};
  for(unsigned py=112;py<144;py++)for(unsigned px=240;px<272;px++)if((MAP_RASTER[(py/8*64+px/8)*8+py%8]>>(px%8))&1){
   count++;if(!tail){queue[tail++]=(py-112)*32+px-240;seen[py-112][px-240]=1;}
  }
  while(head<tail){unsigned q=queue[head++];int px=q%32,py=q/32;
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if((unsigned)(px+dx)<32&&(unsigned)(py+dy)<32&&!seen[py+dy][px+dx]){
    unsigned wx=240+px+dx,wy=112+py+dy;
    if((MAP_RASTER[(wy/8*64+wx/8)*8+wy%8]>>(wx%8))&1){assert(tail<128);seen[py+dy][px+dx]=1;queue[tail++]=(py+dy)*32+px+dx;}
   }
  }
  int du=map_round((x+1)*16,9)-map_round(x*16,9),dv=map_round((y+1)*16,9)-map_round(y*16,9);
  assert(count==4u*(du+dv)&&count==tail);
  for(int vy=y;vy<=y+1;vy++)for(int vx=x;vx<=x+1;vx++){
   unsigned px=map_origin_x(x,y)+map_point_x(vx,vy),py=map_origin_y(x,y)+map_point_y(vx,vy)-2;
   assert((MAP_RASTER[(py/8*64+px/8)*8+py%8]>>(px%8))&1);
  }
 }
}
int main(int argc,char **argv){
 assert(argc==2);setbuf(stdout,0);for(unsigned i=0;i<8;i++)scratch_guard.before[i]=scratch_guard.after[i]=0xa5a5a5a5;
 FILE *f=fopen(argv[1],"rb");assert(f);uint8_t town[9216];assert(fread(town,1,sizeof town,f)==sizeof town);fclose(f);town_pack_collision(town);
 check_junctions();game.area=1;scene("town",0,0);game.area=0;
 for(unsigned level=1;level<=16;level++)for(unsigned seed=1;seed<=8;seed++){
  dungeon_level=level;memset(&game,0,sizeof game);memset(&quest_progress,0,sizeof quest_progress);game_generate_dungeon(&game,seed);
  for(unsigned fog=0;fog<4;fog++)scene("native",seed,fog);
 }
 for(unsigned pattern=0;pattern<8;pattern++){
  memset(game.map,0,sizeof game.map);for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++)game.map[y][x]=(x/(pattern+1)+y/(pattern+1))&1;
  for(unsigned fog=0;fog<4;fog++)scene("synthetic",pattern,fog);
 }

 assert(!native_overflows);printf("{\"native_fallback_by_fog\":[%u,%u,%u,%u]}\n",fallback_fog[0],fallback_fog[1],fallback_fog[2],fallback_fog[3]);
 printf("{\"status\":\"PASS\",\"scenes\":%u,\"pixel_exact_frames\":%u,\"fallback_frames\":%u,\"native_fallback_frames\":%u,\"pixels_compared\":%u,\"unrepresentable_frames\":%u,\"max_native_tiles\":%u,\"max_synthetic_tiles\":%u,\"cached_movement_reuses\":%u,\"edge_recenters\":%u}\n",scenes,frames,fallback_frames,native_fallbacks,frames*38400,overflows,max_native_tiles,max_synthetic_tiles,reuses,recenters);
 return 0;
}
