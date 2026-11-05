/* Retail DRLG_L5FloodTVal/TransFix on the port's archived macro plan. Restore
 * the exact stair/Water search footprints because native flooding precedes
 * those minisets. The same reconstruction runs on generation and revisit. */
#include "world_rooms.h"
#include "coop.h"
#include "../generated/banner_room.h"
#include "../generated/butcher_assets.h"
static void cathedral_banner_base(uint8_t m[40][40]){
 if(!game_campaign_layout()||dungeon_level!=4||!quest_progress.state[7])return;
 unsigned ox=quest_banner_cell%80/2,oy=quest_banner_cell/80/2;
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)if(ox+x<40&&oy+y<40)m[oy+y][ox+x]=banner2[y*8+x]?banner2[y*8+x]:13;
}
void game_rooms_prepare_l1(const Game *g){
 uint8_t (*m)[40]=(void*)dungeon_workspace;
 dungeon_room_macros(g,m);cathedral_banner_base(m);
 for(int y=0;y<40;y++)for(int x=0;x<40;x++)if(m[y][x]==64&&x>=1&&x+2<40&&y>=1&&y+2<40){
  for(int j=0;j<4;j++)for(int i=0;i<4;i++)m[y-1+j][x-1+i]=j==0?22:j==1?2:13;
 }
 if(!game_campaign_layout()||dungeon_level!=4||!quest_progress.state[7]){
  for(int y=1;y<39;y++)for(int x=1;x<38;x++)if(m[y][x]==59){
   for(int j=0;j<3;j++)for(int i=0;i<4;i++)m[y-1+j][x-1+i]=13;
  }
 }
 int water=game_campaign_layout()&&dungeon_level==2&&quest_progress.state[13];
 unsigned wx=quest_water_cell%80/2,wy=quest_water_cell/80/2;
 if(water)for(unsigned y=0;y<6;y++)for(unsigned x=0;x<6;x++)if(wx+x<40&&wy+y<40)m[wy+y][wx+x]=13;
 game_rooms_begin();game_rooms_flood(&m[0][0],40,0,13);
 uint8_t *rooms=game_rooms_candidate();if(!rooms)return;
 if(water)game_rooms_macro_rect((int)wx,(int)wy+2,(int)wx+5,(int)wy+4,0);
 dungeon_room_macros(g,m);cathedral_banner_base(m);
 /* The native loop copies both lower stair64 subcells into its upper row. */
 for(unsigned y=0;y<40;y++)for(unsigned x=0;x<40;x++)if(m[y][x]==64){unsigned xx=2*x+16,yy=2*y+16;rooms[xx*112+yy]=rooms[xx*112+yy+1];rooms[(xx+1)*112+yy]=rooms[(xx+1)*112+yy+1];}
 for(unsigned y=0;y<40;y++)for(unsigned x=0;x<40;x++){
  unsigned p=m[y][x],xx=2*x+16,yy=2*y+16,id=rooms[xx*112+yy];
  /* Guard the two documented native edge reads; valid layouts are interior. */
  if(p==18||(p==23&&y&&m[y-1][x]==18)){rooms[(xx+1)*112+yy]=id;rooms[(xx+1)*112+yy+1]=id;}
  if(p==19||(p==24&&x+1<40&&m[y][x+1]==19)){rooms[xx*112+yy+1]=id;rooms[(xx+1)*112+yy+1]=id;}
  if(p==20){rooms[(xx+1)*112+yy]=id;rooms[xx*112+yy+1]=id;rooms[(xx+1)*112+yy+1]=id;}
 }
 /* DrawButcher allocates a separate room after native TransFix. The RND6
  * architecture persists even after the boss dies or the region is archived. */
 if(dungeon_level==2&&butcher_quest)for(unsigned y=0;y<=34;y++)for(unsigned x=0;x<=34;x++)if(m[y+1][x+1]==168){
  int match=1;for(unsigned j=0;j<6;j++)for(unsigned i=0;i<6;i++)if(m[y+j][x+i]!=butcher_room[j][i])match=0;
  if(match)game_rooms_rect(2*x+19,2*y+19,2*x+26,2*y+26,-1);
 }
 if(game_campaign_layout()&&dungeon_level==4&&quest_progress.state[7]&&quest_progress.stage[7]>=2){int x=quest_banner_cell%80/2,y=quest_banner_cell/80/2;game_rooms_macro_rect(x,y,x+8,y+4,9);}
}
