/* Native transmission helpers, shared by worldgen and authored quest maps.
 * Original algorithms: Devilution 9f01757243412bb154d02d9af65c804159e6e0ea,
 * gendung.cpp and DRLG_L2/L4/L5FTVR. No terrain writes or random calls. */
#include "world_rooms.h"
#include "generation_context.h"

static uint8_t *room_candidate,*room_live;
void game_rooms_generation_capture(GameGenerationState *out){out->room_candidate=room_candidate;out->room_live=room_live;}
void game_rooms_generation_apply(const GameGenerationState *in){room_candidate=in->room_candidate;room_live=in->room_live;}
_Static_assert(GAME_ROOMS_CANDIDATE_OFFSET>64000+256*64,"rooms overlap generator heap");
_Static_assert(GAME_ROOMS_SCRATCH_END<=120000,"rooms overlap prop lookup");
_Static_assert(GAME_ROOMS_LIVE_OFFSET+GAME_ROOMS_LIVE_BYTES<=384*208*2,"rooms exceed renderer arena");

void game_rooms_invalidate(void){room_candidate=0;room_live=0;}
void game_rooms_begin(void){
 room_candidate=dungeon_workspace+GAME_ROOMS_CANDIDATE_OFFSET;
 memset(room_candidate,0,GAME_ROOMS_NATIVE_BYTES);room_candidate[-1]=1;
}
uint8_t *game_rooms_candidate(void){return room_candidate;}
int game_room_candidate_at(int x,int y){
 return room_candidate&&(unsigned)x<80&&(unsigned)y<80?room_candidate[(x+16)*112+y+16]:-1;
}
int game_room_at(int x,int y){
 return room_live&&(unsigned)x<80&&(unsigned)y<80?room_live[y*80+x]:-1;
}
void game_rooms_publish(void){
 if(!room_candidate){room_live=0;return;}
 uint8_t *dst=room_candidate+(GAME_ROOMS_LIVE_OFFSET-GAME_ROOMS_CANDIDATE_OFFSET);
 for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++)dst[y*80+x]=room_candidate[(x+16)*112+y+16];
 room_live=dst;room_candidate=0;
}
static int room_allocate(void){
 /* A zero visit marker would make original FTVR revisit a component forever.
  * Supported generators stay below 256 rooms; reject malformed excess. */
 int id=room_candidate[-1];
 if(!id){room_candidate=0;return -1;}
 room_candidate[-1]=(uint8_t)(id+1);return id;
}
void game_rooms_rect(int x1,int y1,int x2,int y2,int id){
 if(!room_candidate||id< -1||id>255||x1>x2||y1>y2)return;
 if(id<0){id=room_allocate();if(id<0)return;}
 if(x1<0)x1=0;if(y1<0)y1=0;if(x2>111)x2=111;if(y2>111)y2=111;
 for(int x=x1;x<=x2;x++)for(int y=y1;y<=y2;y++)room_candidate[x*112+y]=(uint8_t)id;
}
void game_rooms_macro_rect(int x1,int y1,int x2,int y2,int id){
 /* Bound before doubling to avoid overflow on malformed public coordinates. */
 if(x1>x2||y1>y2||x1>47||y1>47||x2< -8||y2< -8)return;
 if(x1< -9)x1=-9;if(y1< -9)y1=-9;if(x2>48)x2=48;if(y2>48)y2=48;
 game_rooms_rect(2*x1+17,2*y1+17,2*x2+16,2*y2+16,id);
}
void game_rooms_live_rect(int x1,int y1,int x2,int y2,int id){
 if(!room_live||(unsigned)id>255||x1>x2||y1>y2)return;
 if(x1<16)x1=16;if(y1<16)y1=16;if(x2>95)x2=95;if(y2>95)y2=95;
 for(int y=y1;y<=y2;y++)for(int x=x1;x<=x2;x++)room_live[(y-16)*80+x-16]=(uint8_t)id;
}
static void room_stamp(unsigned x,unsigned y,unsigned dir,unsigned id){
 static const uint8_t masks[9]={15,3,12,5,10,8,2,4,1};
 unsigned mask=masks[dir];uint8_t *p=room_candidate+x*112+y;
 if(mask&1)p[0]=(uint8_t)id;if(mask&2)p[1]=(uint8_t)id;
 if(mask&4)p[112]=(uint8_t)id;if(mask&8)p[113]=(uint8_t)id;
}
void game_rooms_flood(const uint8_t *macros,unsigned stride,int x_major,unsigned floor){
 static const int8_t dx[8]={1,-1,0,0,-1,1,-1,1},dy[8]={0,0,1,-1,-1,-1,1,1};
 if(!room_candidate||!macros||stride<40||floor>255)return;
 uint16_t *stack=(void*)(room_candidate+GAME_ROOMS_NATIVE_BYTES);
 for(unsigned j=0;j<40;j++)for(unsigned i=0;i<40;i++){
  unsigned at=x_major?i*stride+j:j*stride+i;
  if(macros[at]!=floor||room_candidate[(i*2+16)*112+j*2+16])continue;
  int id=room_allocate();if(id<0)return;
  unsigned count=1;stack[0]=(uint16_t)(j*40+i);room_stamp(i*2+16,j*2+16,0,(unsigned)id);
  while(count){
   unsigned frame=stack[count-1],dir=frame>>11,cell=frame&2047;
   if(dir==8){count--;continue;}
   stack[count-1]=(uint16_t)(frame+2048);
   int x=(int)(cell%40)+dx[dir],y=(int)(cell/40)+dy[dir];
   unsigned xx=(unsigned)(x*2+16),yy=(unsigned)(y*2+16);
   if((unsigned)x<40&&(unsigned)y<40&&!room_candidate[xx*112+yy]&&
      macros[x_major?(unsigned)x*stride+(unsigned)y:(unsigned)y*stride+(unsigned)x]==floor){
    room_stamp(xx,yy,0,(unsigned)id);stack[count++]=(uint16_t)(y*40+x);
   }else room_stamp(xx,yy,dir+1,(unsigned)id);
  }
 }
}
