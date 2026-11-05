/* Host oracle for the actual prop dirty helper. Expected damage comes only
 * from independently decoded frame pixels supplied by the Python runner. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/game.h"
#include "src/render_coords.h"
#include "generated/prop_frames.h"
#include "generated/explosive_barrels.h"
#include "generated/water_candles.h"

#define UI_FN
enum { WORLD_TILES=31*21, ACTORS=13, FRAME_BYTES=64*64 };
Game game;
static uint16_t town_map[6401];
static int16_t ring_x[WORLD_TILES],ring_y[WORLD_TILES];
static int16_t old_x[WORLD_TILES],old_y[WORLD_TILES];
static int mask_x[ACTORS],mask_y[ACTORS],last_frame[ACTORS];
static unsigned fallback_calls,overlay_calls,death_count;
static struct {int x,y,w,h;} death_rects[64];
static uint8_t native[3][10][FRAME_BYTES];
static uint64_t terrain_checks,actor_checks,retained_masks;
static unsigned cases,pairs_checked,fallback_cases,origins_checked;

static int intersects(int x,int y,int w,int h,int xx,int yy,int ww,int hh){
 return x<xx+ww&&xx<x+w&&y<yy+hh&&yy<y+h;
}
static int floor8(int value){return value>=0?value/8:-((7-value)/8);}
static unsigned independent_slot(int x,int y){
 return (unsigned)((y%21+21)%21*31+(x%31+31)%31);
}
static void death_sprite_invalidate_rect(int x,int y,int w,int h){
 assert(death_count<64);
 death_rects[death_count++]=(typeof(death_rects[0])){x,y,w,h};
}
void game_world_doors_overlay(const Game *g,uint16_t *tiles,uint8_t *depth){
 assert(g==&game&&tiles==town_map&&depth==(uint8_t*)town_map+12800);
 overlay_calls++;
}
/* The runner extracts this unmodified function from current gba_mode0.c,
 * renaming it so the wrapper can observe calls to the fallback. */
#include "stage54_original_invalidate.inc"
static void invalidate_prop(int index){
 fallback_calls++;stage54_original_invalidate_prop(index);
}
#include "src/prop_dirty.inc"

typedef struct {uint64_t color,opacity;} Damage;
static Damage oracle(unsigned group,unsigned before,unsigned after){
 Damage result={0,0};
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  unsigned a=native[group][before][y*64+x],b=native[group][after][y*64+x];
  uint64_t bit=UINT64_C(1)<<((y/8)*8+x/8);
  if(a!=b)result.color|=bit;
  if(!!a!=!!b)result.opacity|=bit;
 }
 assert(!(result.opacity&~result.color));
 return result;
}
static void reset(int x,int y,int relative_x,int relative_y,unsigned trial){
 int tx=floor8(x)-relative_x,ty=floor8(y)-relative_y;
 for(int row=ty;row<ty+21;row++)for(int col=tx;col<tx+31;col++){
  unsigned slot=independent_slot(col,row);ring_x[slot]=col;ring_y[slot]=row;
 }
 /* A matching ring index does not imply matching world coordinates. */
 unsigned target=independent_slot(floor8(x)+(int)(trial%8),floor8(y)+(int)(trial/8%8));
 if(trial%4==1)ring_x[target]+=31;
 if(trial%4==2)ring_y[target]+=21;
 if(trial%4==3)ring_x[target]=ring_y[target]=RING_INVALID;
 memcpy(old_x,ring_x,sizeof(old_x));memcpy(old_y,ring_y,sizeof(old_y));
 uint32_t random=trial+0x748fd902u;
 for(unsigned a=0;a<ACTORS;a++){
  random=random*1664525u+1013904223u;mask_x[a]=x+(int)(random%193)-96;
  random=random*1664525u+1013904223u;mask_y[a]=y+(int)(random%193)-96;
  last_frame[a]=7;
 }
 /* Exact touching edges must not count as intersections. */
 mask_x[0]=x-64;mask_y[0]=y;mask_x[1]=x+64;mask_y[1]=y;
 mask_x[2]=x;mask_y[2]=y-64;mask_x[3]=x;mask_y[3]=y+64;
 mask_x[4]=x;mask_y[4]=y;
 fallback_calls=overlay_calls=death_count=0;
}
static int actor_dirty(unsigned actor,int x,int y,uint64_t opacity){
 for(unsigned ytile=0;ytile<8;ytile++)for(unsigned xtile=0;xtile<8;xtile++)
  if((opacity&(UINT64_C(1)<<(ytile*8+xtile)))&&
     intersects(mask_x[actor],mask_y[actor],64,64,x+(int)xtile*8,y+(int)ytile*8,8,8))return 1;
 return 0;
}
static void verify_damage(int x,int y,Damage damage){
 for(unsigned slot=0;slot<WORLD_TILES;slot++){
  int col=(int)old_x[slot]-floor8(x),row=(int)old_y[slot]-floor8(y);
  int dirty=col>=0&&col<8&&row>=0&&row<8&&
      !!(damage.color&(UINT64_C(1)<<(row*8+col)));
  assert(ring_x[slot]==(dirty?RING_INVALID:old_x[slot]));
  assert(ring_y[slot]==(dirty?RING_INVALID:old_y[slot]));terrain_checks++;
 }
 for(unsigned actor=0;actor<ACTORS;actor++){
  int dirty=actor_dirty(actor,x,y,damage.opacity);
  assert(last_frame[actor]==(dirty?-1:7));actor_checks++;retained_masks+=!dirty;
 }
 uint64_t seen=0;
 for(unsigned i=0;i<death_count;i++){
  int xx=death_rects[i].x-x,yy=death_rects[i].y-y;
  assert(death_rects[i].w==8&&death_rects[i].h==8);
  assert(xx>=0&&xx<64&&yy>=0&&yy<64&&!(xx%8)&&!(yy%8));
  uint64_t bit=UINT64_C(1)<<((yy/8)*8+xx/8);
  assert((damage.opacity&bit)&&!(seen&bit));seen|=bit;
 }
 assert(seen==damage.opacity);
}
static const unsigned counts[]={PROP_BARREL_FRAMES,PROP_BARRELEX_FRAMES,PROP_WATER_CANDLE_FRAMES};
static const unsigned bases[]={PROP_BARREL_BASE,PROP_BARRELEX_BASE,PROP_WATER_CANDLE_BASE};
static const unsigned kinds[]={PROP_BARREL,PROP_EXPLOSIVE_BARREL,PROP_WATER_CANDLE};
static void exact_case(unsigned group,unsigned before,unsigned after,Damage damage,
                       unsigned px,unsigned py,int relative_x,int relative_y,unsigned trial){
 game.props[0]=(Prop){px,py,kinds[group],0};
 int x=1280+((int)px-(int)py)*16-32,y=72+(int)(px+py)*8-64;
 assert(x%8==0&&y%8==0);reset(x,y,relative_x,relative_y,trial);
 Prop unchanged=game.props[0];
 prop_frame_changed(0,bases[group]+before,bases[group]+after);
 assert(!fallback_calls&&!overlay_calls&&!memcmp(&unchanged,&game.props[0],sizeof(Prop)));
 verify_damage(x,y,damage);cases++;
}
static void check_fallback(unsigned kind,unsigned before,unsigned after,unsigned trial){
 game.props[0]=(Prop){0,79,kind,0};
 int x=1280-79*16-32,y=72+79*8-64;
 reset(x,y,(int)(trial%40)-8,(int)(trial/40%30)-8,trial);
 int width=64,height=64;if(game_prop_is_door(kind)){x-=16;y-=24;width=96;height=88;}
 unsigned changed=before!=after;
 prop_frame_changed(0,before,after);
 assert(fallback_calls==changed&&death_count==changed);
 assert(overlay_calls==(changed&&kind>=PROP_L2_DOOR_L&&kind<=PROP_L3_DOOR_R));
 for(unsigned slot=0;slot<WORLD_TILES;slot++){
  int dirty=changed&&intersects(old_x[slot]*8,old_y[slot]*8,8,8,x,y,width,height);
  assert(ring_x[slot]==(dirty?RING_INVALID:old_x[slot]));
  assert(ring_y[slot]==(dirty?RING_INVALID:old_y[slot]));
 }
 for(unsigned a=0;a<ACTORS;a++)
  assert(last_frame[a]==(changed&&intersects(mask_x[a],mask_y[a],64,64,x,y,width,height)?-1:7));
 if(changed)assert(death_rects[0].x==x&&death_rects[0].y==y&&
                   death_rects[0].w==width&&death_rects[0].h==height);
 fallback_cases++;
}
int main(int argc,char **argv){
 assert(argc==4);
 for(unsigned group=0;group<3;group++){
  FILE *input=fopen(argv[group+1],"rb");assert(input);
  assert(fread(native[group],FRAME_BYTES,counts[group],input)==counts[group]);
  assert(fgetc(input)==EOF);assert(!fclose(input));
 }
 static const unsigned positions[][2]={{0,0},{0,79},{79,0},{79,79},{40,40}};
 unsigned serial=0;
 for(unsigned group=0;group<3;group++)for(unsigned before=0;before<counts[group];before++)
  for(unsigned after=0;after<counts[group];after++){
   Damage damage=oracle(group,before,after);pairs_checked++;
   /* Every 8px-aligned placement around the entire 31x21 resident viewport,
    * including all four offscreen borders and every partial clipping extent. */
   for(int y=-8;y<=21;y++)for(int x=-8;x<=31;x++){
    unsigned loc=serial%5;
    exact_case(group,before,after,damage,positions[loc][0],positions[loc][1],x,y,serial++);
   }
  }
 /* Every valid world origin, including negative projected X coordinates. */
 for(unsigned py=0;py<80;py++)for(unsigned px=0;px<80;px++){
  unsigned group=(px+py)&1,before=(px+py*80)%counts[group],after=(before+1)%counts[group];
  exact_case(group,before,after,oracle(group,before,after),px,py,(int)(px%40)-8,(int)(py%30)-8,serial++);
  origins_checked++;
 }
 for(unsigned group=0;group<3;group++){
  const unsigned pairs[][2]={{255,bases[group]},{bases[group],255},
    {bases[group]-1,bases[group]},{bases[group],bases[group]+counts[group]},{255,255}};
  for(unsigned pair=0;pair<5;pair++)for(unsigned trial=0;trial<40;trial++)
   check_fallback(kinds[group],pairs[pair][0],pairs[pair][1],trial*31+pair);
 }
 const unsigned others[]={PROP_CHEST,PROP_SARC,PROP_DECOR_FIRST,PROP_DOOR_L,PROP_DOOR_R,
                         PROP_L2_DOOR_L,PROP_L2_DOOR_R,PROP_L3_DOOR_L,PROP_L3_DOOR_R};
 for(unsigned k=0;k<sizeof(others)/sizeof(others[0]);k++)for(unsigned trial=0;trial<40;trial++){
  check_fallback(others[k],0,1,trial*31+k);check_fallback(others[k],255,0,trial*31+k);
  check_fallback(others[k],0,0,trial*31+k);
 }
 reset(0,0,0,0,0);prop_frame_changed(-1,0,1);prop_frame_changed(MAX_PROPS,0,1);
 assert(!fallback_calls&&!overlay_calls&&!death_count);
 assert(!memcmp(ring_x,old_x,sizeof(ring_x))&&!memcmp(ring_y,old_y,sizeof(ring_y)));
 for(unsigned a=0;a<ACTORS;a++)assert(last_frame[a]==7);
 printf("{\"status\":\"PASS\",\"frame_pairs\":%u,\"pixel_oracle_cases\":%u,"
        "\"terrain_slots_checked\":%llu,\"actor_masks_checked\":%llu,"
        "\"retained_actor_masks\":%llu,\"world_origins\":%u,\"fallback_cases\":%u}\n",
        pairs_checked,cases,(unsigned long long)terrain_checks,(unsigned long long)actor_checks,
        (unsigned long long)retained_masks,origins_checked,fallback_cases);
 return 0;
}
