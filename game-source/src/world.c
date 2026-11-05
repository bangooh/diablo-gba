#include "world.h"
#include "world_rooms.h"
#include "generation_context.h"
#include "../generated/fixed_rooms.h"
#include "../generated/late_quests.h"
#include "coop.h"
#include "../generated/finale_assets.h"
uint16_t quest_diablo_cells[4];
uint8_t game_cave_policy;
#include "worldgen.h"
#include "../generated/world_art.h"
#include "../generated/king_quest.h"
#include "../generated/water_quest.h"
const WorldArt *game_world_art(unsigned level){return level==REGION_BONE?&l2_art:level==REGION_WATER?&l3_art:(level==REGION_KING||level==REGION_VILE)?&l1_art:level<9?&l2_art:level<13?&l3_art:&l4_art;}

/* Cosmetic clock: never serialized; a loaded completed quest is already clean. */
static uint8_t water_phase;
void game_water_colors_reset(void){water_phase=0;}
int game_water_colors_step(const Game *g,unsigned previous_state){
 if(g->area||dungeon_level!=REGION_WATER||quest_progress.state[13]!=3){water_phase=0;return 0;}
 if(g->paused||g->won||g->hp<=0)return 0;
 if(previous_state!=3){water_phase=32;return 1;}
 if(!water_phase)return 0;
 --water_phase;return water_phase!=0;
}
unsigned game_water_palette_state(unsigned level){
 if(level!=REGION_WATER)return 0;
 if(quest_progress.state[13]!=3)return 1;
 return water_phase?34-water_phase:33;
}
static void water_map(Game *g,uint16_t *tiles,uint8_t *depth){
 const WorldArt *a=&l3_art;
 for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++){
  unsigned macro=x/2<19&&y/2<37?water_macros[(y/2)*19+x/2]:8;if(!macro)macro=7;
  unsigned p=a->macros[(macro-1)*4+(y&1)*2+(x&1)];
  if(p==530)p=537;else if(p==533)p=540;
  if(g)g->map[y][x]=!a->solid[p];if(tiles)tiles[y*80+x]=p;if(depth)depth[y*80+x]=a->occludes[p];
 }
 if(g){g->map[67][14]=2;g->x=15;g->y=67;g->down_x=g->down_y=0;}
}
/* Preserve only the native open room, not the entire generator workspace.
 * A live pedestal action must not overwrite renderer or archive scratch. */
static uint8_t valor_open[11*17];
void game_world_generation_capture(GameGenerationState *out){
 for(unsigned i=0;i<4;i++)out->quest_cells[5+i]=quest_diablo_cells[i];
 for(unsigned i=0;i<sizeof valor_open;i++)out->valor_open[i]=valor_open[i];out->water_phase=water_phase;out->cave_policy=game_cave_policy;
}
void game_world_generation_apply(const GameGenerationState *in){
 for(unsigned i=0;i<4;i++)quest_diablo_cells[i]=in->quest_cells[5+i];
 for(unsigned i=0;i<sizeof valor_open;i++)valor_open[i]=in->valor_open[i];water_phase=in->water_phase;game_cave_policy=in->cave_policy;
}
static void layout(uint32_t seed,int *up,int *down){
 quest_valor_cell=quest_anvil_cell=0;
 if(game_campaign_layout()&&((dungeon_level==6&&quest_progress.state[14])||(dungeon_level==13&&quest_progress.state[11]))){int origin[2];world_generate_late(dungeon_level,seed,(quest_progress.stage[dungeon_level==6?14:11]&63),dungeon_workspace,up,down,origin);quest_valor_cell=(uint16_t)(origin[1]*160+origin[0]*2);world_late_open_tiles(valor_open,dungeon_level==6?7:8,7);return;}
 if(dungeon_level==7&&game_campaign_layout()&&quest_progress.state[8]){
  int origin[2];world_generate_blind(seed,quest_progress.stage[8],dungeon_workspace,up,down,origin);quest_valor_cell=(uint16_t)(origin[1]*160+origin[0]*2);world_blind_open_tiles(valor_open);return;
 }
 if(dungeon_level==5&&game_campaign_layout()&&quest_progress.state[9]){
  int origin[2];unsigned stage=quest_progress.stage[9];world_generate_valor(seed,stage?stage-1:0,dungeon_workspace,up,down,origin);
  quest_valor_cell=(uint16_t)(origin[1]*160+origin[0]*2);world_valor_open_tiles(valor_open);
 }else{int cell=0;world_generate_quest(dungeon_level,seed,dungeon_workspace,up,down,game_campaign_layout()&&quest_progress.state[10],&cell);quest_anvil_cell=(uint16_t)cell;if(dungeon_level==16)world_diablo_layout(quest_progress.finale_flags>>4,dungeon_workspace,quest_diablo_cells);}
}
static unsigned piece(const WorldArt *a,unsigned id,unsigned sub){unsigned p=id&&id<=a->macro_count?a->macros[(id-1)*4+sub]:0;
 /* Native open-door piece substitutions from OperateL2/L3Door. */
 if((dungeon_level==REGION_KING||dungeon_level==REGION_VILE)){if(p==43||p==50)p=392;else if(p==213)p=407;else if(p==45||p==55)p=394;}
 else if(dungeon_level<9||dungeon_level==REGION_BONE){if(p==540)p=12;else if(p==541)p=16;}
 else if(dungeon_level<13){if(p==530)p=537;else if(p==533)p=540;}
 return p;}
/* Door records share the normal prop archive; there is no side state to lose on
 * a revisit. Native dPiece IDs are one-based, our WorldArt IDs are zero-based. */
#include "world_doors.inc"
/* AddSKingObjs uses three inclusive macro rectangles from SklKng1.
 * Fixed-map doors currently use the port's open-door adapter. */
void game_king_change(Game *g,uint16_t *tiles,uint8_t *depth){
 if(dungeon_level!=REGION_KING)return;
 uint8_t *m=dungeon_workspace;for(unsigned i=0;i<1600;i++)m[i]=22;
 for(unsigned y=0;y<25;y++)for(unsigned x=0;x<37;x++)m[y*40+x]=sklkng2[y*37+x]?sklkng2[y*37+x]:13;
 for(unsigned i=0;i<3;i++)if(quest_progress.king_gates&(1u<<i)){const uint8_t *r=king_gate_rects[i];for(unsigned y=r[1];y<=r[3];y++)for(unsigned x=r[0];x<=r[2];x++)m[y*40+x]=sklkng1[y*37+x]?sklkng1[y*37+x]:13;}
 const WorldArt *a=&l1_art;
 for(int y=79;y>=0;y--)for(int x=79;x>=0;x--){unsigned p=piece(a,m[(y/2)*40+x/2],(y&1)*2+(x&1));
  if(g)g->map[y][x]=!a->solid[p]||p==392||p==394||p==407;
  if(tiles)tiles[y*80+x]=p;if(depth)depth[y*80+x]=a->occludes[p];
 }
 if(g)g->map[26][66]=2;
}
/* ObjChangeMap rectangles from AddVileObjs, cropped by the native16-cell border. */
void game_vile_change(Game *g,uint16_t *tiles,uint8_t *depth){
 if(dungeon_level!=REGION_VILE)return;
 const WorldArt *a=&l1_art;
 static const uint8_t rects[4][4]={{1,1,9,10},{11,1,20,10},{7,11,13,18},{1,18,20,24}};
 for(int y=79;y>=0;y--)for(int x=79;x>=0;x--){
  unsigned mx=x/2,my=y/2,macro=22;
  if(mx<21&&my<23){int opened=0;for(unsigned n=0;n<4;n++)if((quest_progress.finale_flags&(1u<<n))&&mx>=rects[n][0]&&mx<=rects[n][2]&&my>=rects[n][1]&&my<=rects[n][3])opened=1;
   macro=(opened?finale_vile1:finale_vile2)[my*21+mx];if(!macro)macro=13;}
  unsigned p=piece(a,macro,(y&1)*2+(x&1));
  if(g)g->map[y][x]=!a->solid[p]||p==392||p==394||p==407;
  if(tiles)tiles[y*80+x]=p;if(depth)depth[y*80+x]=a->occludes[p];
 }
}
/* Opening16th-floor seals changes only their native rectangles. */
void game_diablo_change(Game *g,uint16_t *tiles,uint8_t *depth){
 if(dungeon_level!=16)return;
 static const uint8_t widths[3]={11,11,9},heights[3]={12,11,9};
 const uint8_t *maps[3]={finale_diab2b,finale_diab3b,finale_diab4b};const WorldArt *a=&l4_art;
 for(unsigned q=0;q<3;q++)if(quest_progress.finale_flags&(16u<<q)){
  int ox=2*(quest_diablo_cells[q+1]%40),oy=2*(quest_diablo_cells[q+1]/40);
  for(unsigned y=0;y<heights[q];y++)for(unsigned x=0;x<widths[q];x++)for(unsigned sub=0;sub<4;sub++){
   /* DRLG_L4SetRoom maps zero DUN cells to floor 6; skipping them
    * retains closed-room walls, especially around Diablo. */
   unsigned macro=maps[q][y*widths[q]+x];if(!macro)macro=6;
   unsigned p=piece(a,macro,sub);int xx=ox+2*x+(sub&1),yy=oy+2*y+(sub>>1);if(xx>=80||yy>=80)continue;
   if(g&&g->map[yy][xx]!=2)g->map[yy][xx]=!a->solid[p];if(tiles)tiles[yy*80+xx]=p;if(depth)depth[yy*80+xx]=a->occludes[p];
  }
 }
}
void game_world_valor_change(Game *g,uint16_t *tiles,uint8_t *depth){
 int blind=dungeon_level==7&&quest_progress.state[8]&&quest_progress.stage[8];
 if(!game_campaign_layout()||(!blind&&(dungeon_level!=5||!quest_progress.state[9]||quest_progress.stage[9]<2)))return;
 unsigned stones=blind?3:quest_progress.stage[9]-1;int ox=quest_valor_cell%80,oy=quest_valor_cell/80,width=blind?13:11,height=blind?13:17;const WorldArt *a=game_world_art(dungeon_level);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  if(stones<3&&!(y>=3&&y<=7&&(x<=2||(stones>=2&&x>=6))))continue;
  for(unsigned sub=0;sub<4;sub++){
   unsigned p=piece(a,valor_open[y*width+x],sub);int xx=ox+x*2+(sub&1),yy=oy+y*2+(sub>>1),c=yy*80+xx;
   if(g&&g->map[yy][xx]!=2){g->map[yy][xx]=(uint8_t)(!a->solid[p]||p==12||p==16);world_door_add(g,xx,yy,p);}
   if(tiles)tiles[c]=p;if(depth)depth[c]=a->occludes[p];
  }
 }
}
static void nearest(const Game *g,int *x,int *y){
 int bx=*x,by=*y,best=100000;
 for(int y1=1;y1<79;y1++)for(int x1=1;x1<79;x1++)if(g->map[y1][x1]){int dx=x1-bx,dy=y1-by,d=dx*dx+dy*dy;if(d<best){best=d;*x=x1;*y=y1;}}
}
/* Fixed quest maps carry authored transmission regions rather than a flood. */
void game_rooms_prepare_fixed(unsigned level){
 game_rooms_begin();
 if(level==REGION_KING||level==REGION_BONE){
  const uint8_t (*rects)[5]=level==REGION_KING?fixed_king_rooms:fixed_bone_rooms;
  unsigned count=level==REGION_KING?sizeof fixed_king_rooms/5:sizeof fixed_bone_rooms/5;
  for(unsigned i=0;i<count;i++)game_rooms_rect(rects[i][0],rects[i][1],rects[i][2],rects[i][3],rects[i][4]);
 }else if(level==REGION_VILE){
  uint8_t *rooms=game_rooms_candidate();if(!rooms)return;
  for(unsigned y=0;y<FIXED_VILE_ROOM_HEIGHT;y++)for(unsigned x=0;x<FIXED_VILE_ROOM_WIDTH;x++){
   unsigned i=y*FIXED_VILE_ROOM_WIDTH+x;rooms[(x+16)*112+y+16]=(fixed_vile_room_bits[i>>3]>>(i&7))&1;
  }
 }
 /* Poisoned Water deliberately leaves every native room ID at zero. */
}
void game_world_generate(Game *g,uint32_t seed){
 if(dungeon_level>=REGION_KING&&dungeon_level<=REGION_BONE)game_rooms_prepare_fixed(dungeon_level);
 if(dungeon_level==REGION_BONE){game_late_change(g,0,0);g->x=53;g->y=23;g->down_x=g->down_y=0;return;}
 if(dungeon_level==REGION_VILE){game_vile_change(g,0,0);g->x=19;g->y=20;g->down_x=g->down_y=0;return;}
 if(dungeon_level==REGION_WATER){water_map(g,0,0);return;}
 if(dungeon_level==REGION_KING){game_king_change(g,0,0);g->x=67;g->y=29;g->down_x=g->down_y=0;return;}
 int up[2],down[2];layout(seed,up,down);const WorldArt *a=game_world_art(dungeon_level);
 for(int y=0;y<80;y++)for(int x=0;x<80;x++){
  unsigned p=piece(a,dungeon_workspace[(y/2)*40+x/2],(y&1)*2+(x&1));
  /* Walkable doorway terrain is blocked by its archived closed-door prop. */
  unsigned door=dungeon_level<9?(p==12||p==16||p==540||p==541):(dungeon_level<13&&(p==530||p==533));
  g->map[y][x]=(uint8_t)(!a->solid[p]||door);
  /* Original InitL2/L3/L4Triggers dPiece559/549/422, minus one. */
  if((dungeon_level==5&&p==558)||(dungeon_level==9&&p==548)||(dungeon_level==13&&p==421))g->map[y][x]=3;
 }
 if(dungeon_level==16)game_diablo_change(g,0,0);
 nearest(g,&up[0],&up[1]);nearest(g,&down[0],&down[1]);
 g->map[up[1]][up[0]]=2;g->x=up[0];g->y=up[1];
 g->down_x=dungeon_level<16?down[0]:0;g->down_y=dungeon_level<16?down[1]:0;
}
/* Caller owns a freshly generated, intact macro map in dungeon_workspace. */
void game_world_visuals_prepared(uint16_t *tiles,uint8_t *depth){
 const WorldArt *a=game_world_art(dungeon_level);
 /* Layout at scratch start overlaps the destination; expand backwards. */
 for(int y=79;y>=0;y--)for(int x=79;x>=0;x--){unsigned p=piece(a,dungeon_workspace[(y/2)*40+x/2],(y&1)*2+(x&1));tiles[y*80+x]=p;depth[y*80+x]=a->occludes[p];}
 if(dungeon_level==16)game_diablo_change(0,tiles,depth);
}
void game_world_visuals(uint16_t *tiles,uint8_t *depth,uint32_t seed){
 if(dungeon_level>=REGION_KING&&dungeon_level<=REGION_BONE)game_rooms_prepare_fixed(dungeon_level);
 if(dungeon_level==REGION_BONE){game_late_change(0,tiles,depth);return;}
 if(dungeon_level==REGION_VILE){game_vile_change(0,tiles,depth);return;}
 if(dungeon_level==REGION_WATER){water_map(0,tiles,depth);return;}
 if(dungeon_level==REGION_KING){game_king_change(0,tiles,depth);return;}
 int up[2],down[2];layout(seed,up,down);
 game_world_visuals_prepared(tiles,depth);
}

void game_late_change(Game *g,uint16_t *tiles,uint8_t *depth){
 const WorldArt *a=game_world_art(dungeon_level);
 if(dungeon_level==REGION_BONE){
  static const uint8_t rect[2][4]={{17,0,21,5},{13,0,16,5}};
  for(int y=79;y>=0;y--)for(int x=79;x>=0;x--){unsigned mx=x/2,my=y/2,id=12;
   if(mx<32&&my<18){const uint8_t *map=late_bonecha1;for(unsigned i=0;i<2;i++)if((quest_progress.bone_gates&(1u<<i))&&mx>=rect[i][0]&&mx<=rect[i][2]&&my>=rect[i][1]&&my<=rect[i][3])map=late_bonecha2;id=map[4+2*(my*32+mx)];if(!id)id=3;}
   unsigned p=piece(a,id,(y&1)*2+(x&1));if(g)g->map[y][x]=!a->solid[p]||p==12||p==16;if(tiles)tiles[y*80+x]=p;if(depth)depth[y*80+x]=a->occludes[p];
  }
  if(g){g->map[23][54]=2;game_world_doors(g,0);}return;
 }
 if(!game_campaign_layout()||(dungeon_level!=6&&dungeon_level!=13)||!quest_progress.stage[dungeon_level==6?14:11])return;
 unsigned w=dungeon_level==6?7:8;int ox=quest_valor_cell%80,oy=quest_valor_cell/80;
 for(unsigned y=0;y<7;y++)for(unsigned x=0;x<w;x++)for(unsigned sub=0;sub<4;sub++){
  unsigned p=piece(a,valor_open[y*w+x],sub);int xx=ox+2*x+(sub&1),yy=oy+2*y+(sub>>1);if(xx>=80||yy>=80)continue;
  if(g&&g->map[yy][xx]!=2){g->map[yy][xx]=!a->solid[p]||(dungeon_level==6&&(p==12||p==16));world_door_add(g,xx,yy,p);}if(tiles)tiles[yy*80+xx]=p;if(depth)depth[yy*80+xx]=a->occludes[p];
 }
}
