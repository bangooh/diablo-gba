/* Stage80: bounded tile jobs; actor raw caches (load_buffer+2048) stay intact.
 * BG2 is a256x256 ring. Only32x24 cells are resident; old off-screen rows
 * release dictionary references before new edges are populated. */
#include "../generated/town_minimap80.h"
#define MS_REFS ((uint16_t*)(MAP_OUTLINE+1180))
#define MS_DIRTY (MAP_OUTLINE+1808)
#define MS_HASH (MAP_REVEALED+1180)
#define MS_FOG (MAP_REVEALED+1496)
typedef struct {int16_t x,y;uint16_t cursor,refs,pixels,ready;uint32_t revision;uint16_t used;} MapStream;
#define MS ((MapStream*)(MAP_OUTLINE+1936))
_Static_assert(1936+sizeof(MapStream)<=2048,"stream state exceeds outline tail");
_Static_assert(1496+200<=1800,"stream fog exceeds edge tail");
typedef struct {uint16_t at[96],value[96],slot[8];uint32_t pixels[8][8];} MapBatch;
#define MB ((MapBatch*)load_buffer)
_Static_assert(sizeof(MapBatch)<=2048,"stream payload overlaps town actor cache");
static inline unsigned ms_tile(unsigned n){return n<298?outer_map_tile(n):501+n-298;}
static inline unsigned ms_slot(unsigned t){return t>=530&&t<704?t-530:t>=896&&t<960?t-896+174:t>=266&&t<310?t-266+238:t>=326&&t<342?t-326+282:t>=501&&t<512?t-501+298:0;}
static inline unsigned ms_ref(unsigned n){return (MAP_VRAM_WORD(0x0600e800u+(n&~1u)*2)>>(16*(n&1)))&65535;}
static inline void ms_dirty(unsigned n){MS_DIRTY[n>>3]|=1u<<(n&7);}
static inline int ms_floor(int n,int d){return n>=0?n/d:-((-n+d-1)/d);}
static UI_FN void ms_camera(void){automap_camera();}
static UI_FN void ms_write(unsigned n,unsigned value){unsigned i=MS->refs++;MB->at[i]=(uint16_t)n;MB->value[i]=(uint16_t)value;}
static UI_FN unsigned ms_current(unsigned n){for(unsigned i=MS->refs;i;i--)if(MB->at[i-1]==n)return MB->value[i-1];return ms_ref(n);}
static UI_FN void ms_clear(unsigned n){unsigned old=ms_slot(ms_current(n)&1023);if(old&&MS_REFS[old])--MS_REFS[old];ms_write(n,ms_tile(0)|((map_line_color>>4)<<12));}
static inline unsigned ms_seen(int x,int y){if(x<0||y<0||x>=80||y>=80)return 0;unsigned n=(y/2)*40+x/2;return (MS_FOG[n>>3]>>(n&7))&1;}

static inline void ms_line(uint8_t *bits,int tx,int ty,int x,int y,unsigned axis){
 int a=4*(x-y),b=4*(x+!axis-y-axis);if(a>b){int c=a;a=b;b=c;}
 int left=tx*8,top=ty*8;if(a<left)a=left;if(b>left+7)b=left+7;
 int intercept=8*(axis?x:y)-4;
 for(int px=a;px<=b;px++){int py=ms_floor((axis?-px:px)+intercept,2);if(py>=top&&py<top+8)bits[py-top]|=(uint8_t)(1u<<(px-left));}
}
static inline uint32_t ms_flip_x(uint32_t v){
 v=((v>>1)&0x55555555u)|((v&0x55555555u)<<1);
 v=((v>>2)&0x33333333u)|((v&0x33333333u)<<2);
 return ((v>>4)&0x0f0f0f0fu)|((v&0x0f0f0f0fu)<<4);
}
static UI_FN unsigned ms_pixels(int tx,int ty,uint32_t out[8]){
 uint32_t words[2]={0,0};uint8_t *bits=(uint8_t*)words;
 if(game.area){int x=tx+44,y=ty+1;if(x>=0&&x<TOWN_MAP80_W&&y>=0&&y<TOWN_MAP80_H){unsigned n=town_map80_refs[!!game_wounded_present()][y*TOWN_MAP80_W+x];words[0]=town_map80_tiles[n][0];words[1]=town_map80_tiles[n][1];}}
 else{
  /* Each diagonal family crosses only a narrow strip of source edges.
   * Dungeon vertices use a fixed two-pixel step. */
  int left=tx*8,right=left+7,top=ty*8,bottom=top+7;
  for(unsigned axis=0;axis<2;axis++){
   int lo=ms_floor((2*top+(axis?left:-right)+4),8)-1;
   int hi=ms_floor((2*bottom+(axis?right:-left)+4),8)+1;
   if(lo<0)lo=0;if(hi>80)hi=80;
   for(int fixed=lo;fixed<=hi;fixed++){
    int v=4*fixed;
    int first=ms_floor((axis?v-right:left+v),4)-1;
    int last=ms_floor((axis?v-left:right+v),4)+1;
    if(first<0)first=0;if(last>79)last=79;
    for(int run=first;run<=last;run++){
     int x=axis?fixed:run,y=axis?run:fixed;
     if(map_edge(axis?MAP_REVEALED:MAP_OUTLINE,run,fixed)&&(ms_seen(x,y)||ms_seen(x-!!axis,y-!axis)))ms_line(bits,tx,ty,x,y,axis);
    }
   }
  }
 }
 unsigned flip=0;uint32_t a=words[0],b=words[1],oa=a,ob=b;
 for(unsigned f=1;!game.area&&f<4;f++){
  uint32_t h=f&1?ms_flip_x(oa):oa,j=f&1?ms_flip_x(ob):ob;
  if(f&2){uint32_t old=h;h=__builtin_bswap32(j);j=__builtin_bswap32(old);}
  if(h<a||(h==a&&j<b)){a=h;b=j;flip=f<<10;}
 }
 words[0]=a;words[1]=b;
 static const uint16_t expand[16]={0,1,16,17,256,257,272,273,4096,4097,4112,4113,4352,4353,4368,4369};
 unsigned ink=map_line_color&15;
 for(unsigned y=0;y<8;y++)out[y]=(expand[bits[y]&15]|((uint32_t)expand[bits[y]>>4]<<16))*ink;
 return flip;
}
static UI_FN unsigned ms_find(const uint32_t pixels[8]){
 uint32_t h=0;for(unsigned y=0;y<8;y++)h=(h*33u)^pixels[y];uint8_t hash=(uint8_t)(h^(h>>8)^(h>>16));
 unsigned blank=0;for(unsigned y=0;y<8;y++)blank|=pixels[y];if(!blank)return 0;
 unsigned free=309;
 for(unsigned n=1;n<MS->used;n++){
  if(!MS_REFS[n]){if(free==309)free=n;continue;}
  if(MS_HASH[n]!=hash)continue;
  const uint32_t *p=0;for(unsigned j=MS->pixels;j;j--)if(MB->slot[j-1]==n){p=MB->pixels[j-1];break;}
  unsigned same=1;for(unsigned y=0;y<8;y++)if(pixels[y]!=(p?p[y]:MAP_VRAM_WORD(0x06008000u+ms_tile(n)*32+y*4))){same=0;break;}
  if(same)return n;
 }
 if(free==309&&MS->used<309)free=MS->used;
 if(free==309){map_tile_overflows++;return 309;}
 if(MS->pixels==8)return 310;
 if(free==MS->used)++MS->used;
 unsigned j=MS->pixels++;MB->slot[j]=(uint16_t)free;for(unsigned y=0;y<8;y++)MB->pixels[j][y]=pixels[y];MS_HASH[free]=hash;
 return free;
}
static UI_FN void ms_reveal(void){
 if(game.area)return;
 uint8_t fog[200];game_explore_export(fog);
 for(unsigned i=0;i<200;i++)if(fog[i]!=MS_FOG[i]){
  unsigned change=fog[i]^MS_FOG[i];
  for(unsigned b=0;b<8;b++)if(change&(1u<<b)){
   unsigned cell=i*8+b;int x=(cell%40)*2,y=(cell/40)*2;
   int left=ms_floor(map_point_x(x,y+2)-2,8),right=ms_floor(map_point_x(x+2,y)+2,8);
   int top=ms_floor(map_point_y(x,y)-4,8),bottom=ms_floor(map_point_y(x+2,y+2)+2,8);
   for(int yy=top;yy<=bottom;yy++)for(int xx=left;xx<=right;xx++)if(xx>=MS->x&&xx<MS->x+32&&yy>=MS->y&&yy<MS->y+24)ms_dirty(((yy&31)<<5)|(xx&31));
  }
  MS_FOG[i]=fog[i];
 }
}
static UI_FN void automap_stream_draw(void){
 if(!map_visible||map_pending)return;
 ms_camera();int nx=ms_floor(map_ref_x-120,8)-1,ny=ms_floor(map_ref_y-80,8)-2;
 /* Keep the resident window until its margin is consumed. In particular,
  * reversing across one 8px boundary must not evict/rebuild the same strip. */
 if(map_last_x!=-1&&map_last_area==game.area){
  int left=ms_floor(map_ref_x-120,8),top=ms_floor(map_ref_y-80,8);
  if(left>=MS->x&&left+30<MS->x+32)nx=MS->x;
  if(top>=MS->y&&top+20<MS->y+24)ny=MS->y;
  /* A one-row shift keeps entering plus outgoing references below80. */
  if(ny>MS->y+1)ny=MS->y+1;else if(ny<MS->y-1)ny=MS->y-1;
 }
 unsigned revision=game.area?!!game_wounded_present():game_explore_revision();
 if(map_last_x==-1||map_last_area!=game.area||(game.area&&MS->revision!=revision)){
  if(!game.area)automap_build_outline();MS->revision=revision;for(unsigned n=0;n<309;n++){MS_REFS[n]=0;MS_HASH[n]=0;}
  for(unsigned n=0;n<128;n++)MS_DIRTY[n]=0;
  for(unsigned n=0;n<200;n++)MS_FOG[n]=0;
  MS->x=(int16_t)nx;MS->y=(int16_t)ny;MS->cursor=MS->refs=MS->pixels=MS->ready=0;MS->used=1;
  for(int y=ny;y<ny+24;y++)for(int x=nx;x<nx+32;x++)ms_dirty(((y&31)<<5)|(x&31));
  map_last_x=game.x;map_last_y=game.y;map_last_area=game.area;map_screen_fallback=map_screen_live=0;map_init=1;map_pending=3;return;
 }
 if(MS->ready&&nx==MS->x&&ny==MS->y&&MS->revision==revision)return;
 unsigned reveal=MS->revision!=revision;MS->revision=revision;MS->refs=MS->pixels=0;
 int moved=nx!=MS->x||ny!=MS->y;
 if(moved){
  /* Visit only entering strips, rather than scanning768 resident cells. */
  int ox=MS->x,oy=MS->y;
  for(int y=ny;y<ny+24;y++)for(int x=nx;x<nx+32;x++){
   if(y>=oy&&y<oy+24&&x>=ox&&x<ox+32){x=ox+31;if(x>=nx+32)break;continue;}
   unsigned n=((y&31)<<5)|(x&31);ms_dirty(n);if(MS->refs<80)ms_clear(n);
  }
  /* Only outgoing rows can retain dictionary references outside the ring. */
  for(int y=oy;y<oy+24&&MS->refs<80;y++){
   if(y>=ny&&y<ny+24){y=ny+23;continue;}
   for(unsigned x=0;x<32&&MS->refs<80;x++){
    unsigned n=((y&31)<<5)|x;MS_DIRTY[n>>3]&=~(1u<<(n&7));if(ms_slot(ms_current(n)&1023))ms_clear(n);
   }
  }
  MS->x=(int16_t)nx;MS->y=(int16_t)ny;
 }
 if(reveal||map_init)ms_reveal();unsigned examined=0,visited=0,exhausted=0;
 while(visited++<1024&&examined<(game.area||map_init?24u:6u)&&MS->refs<96){
  unsigned n=MS->cursor;MS->cursor=(n+1)&1023;if(!(MS_DIRTY[n>>3]&(1u<<(n&7))))continue;
  examined++;int x=nx+(((int)(n&31)-nx)&31),y=ny+(((int)(n>>5)-ny)&31);
  if(y>=ny+24){MS_DIRTY[n>>3]&=~(1u<<(n&7));continue;}
  uint32_t pixels[8];unsigned flip=ms_pixels(x,y,pixels);
  unsigned old=ms_slot(ms_current(n)&1023);if(old&&MS_REFS[old])--MS_REFS[old];
  unsigned slot=ms_find(pixels);
  if(slot>=309){if(old)++MS_REFS[old];exhausted=slot==309;break;}
  if(slot)++MS_REFS[slot];ms_write(n,ms_tile(slot)|flip|((map_line_color>>4)<<12));MS_DIRTY[n>>3]&=~(1u<<(n&7));
 }
 unsigned remaining=0;for(unsigned n=0;n<128;n++)remaining|=MS_DIRTY[n];MS->ready=exhausted?2:!remaining;
 /* Unrepresentable adversarial maps must not retry every idle frame. */
 if(exhausted)map_init=1;
 map_last_x=game.x;map_last_y=game.y;map_last_revision=game_explore_revision();
 unsigned used=1;for(unsigned n=1;n<MS->used;n++)used+=MS_REFS[n]!=0;map_unique_tiles=used;
 if(MS->refs||MS->pixels)map_pending=2;
}
static UI_FN void automap_stream_commit(void){
 if(!map_pending)return;
 /* An audio mix IRQ can exceed the remaining blanking window. Mask IRQs
  * only for this bounded batch, never while waiting for the next VBlank.
  * Recheck VCOUNT after acquiring IME so a just-serviced IRQ cannot move the
  * actual start past the deadline. DMA/audio hardware keeps running. */
 unsigned latest=map_pending==3?190u:PERF_MAP_WINDOW?218u:210u,ime;
 for(;;){
  unsigned before;do{before=R16(0x04000006);}while(before<160||before>latest);
  ime=R16(0x04000208);R16(0x04000208)=0;
  before=R16(0x04000006);if(before>=160&&before<=latest)break;
  R16(0x04000208)=ime;
 }
 if(map_pending==3){
  uint32_t blank=(ms_tile(0)|((map_line_color>>4)<<12));blank|=blank<<16;
  for(unsigned i=0;i<512;i++)R16(0x0600e800u+i*4)=blank&65535,R16(0x0600e802u+i*4)=blank>>16;
  for(unsigned i=0;i<8;i++)R16(0x06008000u+ms_tile(0)*32+i*4)=0,R16(0x06008002u+ms_tile(0)*32+i*4)=0;
 }else{
  for(unsigned j=0;j<MS->pixels;j++)dma(MB->pixels[j],(void*)(0x06008000u+ms_tile(MB->slot[j])*32),32);
  for(unsigned j=0;j<MS->refs;j++)R16(0x0600e800u+MB->at[j]*2)=MB->value[j];
  if(MS->ready==1)map_init=0;
 }
 R16(0x0400000c)=OUTER_MAP_BG2_CONTROL;map_pending=0;map_commits++;
 if(R16(0x04000006)<160)map_commit_overruns++;
 R16(0x04000208)=ime;
}
