/* Right-corner minimap (Options > Minimap: Right). Top-down cells turned
 * 45 degrees clockwise to follow the isometric screen, at a quarter of the
 * full map's width: pixel (P,Q) holds cell (x,y)'s centre at (x-y,x+y+1) and
 * its top corner at (x-y,x+y). Only contour corners are drawn, so walls are
 * thin one-pixel diagonals; they use the full map's seen-edge rule. A door,
 * or the notch above the cathedral down stairs, clears the four corners of
 * its cell: with the empty centre, a five-pixel cross.
 *
 * Like the full map, the hero stays at the centre and the map moves under
 * it pixel by pixel. The 64x64 box is eight by eight fixed BG2 tiles that
 * never scroll, so nothing can show outside the box (hardware lighting owns
 * WIN0/WIN1). A 1bpp copy of the box shifts with the camera; only the
 * entering rows/columns and freshly explored cells are evaluated, then the
 * 64 tiles are expanded and uploaded in one 2KiB DMA.
 *
 * Bitsets reuse the streaming map's storage: 96x96 walkable cells in
 * MAP_OUTLINE, door cells in MAP_REVEALED, the box bitmap in the
 * idle dictionary reference table, and the expanded tiles in the map's
 * load_buffer payload. Fog and commit timing are the stream's. */
enum { MC_BOX_X=174, MC_BOX_Y=2, MC_SIZE=64, MC_TILE0=1, MC_STEP=4 };
_Static_assert(96*96/8<=1180,"corner map bitsets reach stream tails");
_Static_assert(MC_BOX_X+MC_SIZE<=240&&MC_BOX_Y+MC_SIZE<=160,"corner map box leaves the screen");
_Static_assert(MC_TILE0+64<=174,"corner tiles must be one contiguous dictionary run");
#define MC_ROWS ((uint32_t (*)[2])(MAP_OUTLINE+1180))
_Static_assert(1180+MC_SIZE*8<=1808,"corner bitmap overlaps stream dirty bits");
#define MC_STAGE load_buffer
_Static_assert(MC_SIZE*MC_SIZE/2<=2048,"corner tiles overlap actor raw caches");
#define MC_WALK MAP_OUTLINE
#define MC_DOOR MAP_REVEALED
static inline __attribute__((always_inline)) unsigned mc_bit(const uint8_t *bits,int x,int y){
 if((unsigned)x>=96u||(unsigned)y>=96u)return 0;
 unsigned n=(unsigned)y*96u+(unsigned)x;return (bits[n>>3]>>(n&7))&1;
}
static inline __attribute__((always_inline)) unsigned mc_seen(int x,int y){return game.area?(unsigned)x<96u&&(unsigned)y<96u:ms_seen(x,y);}
static inline __attribute__((always_inline)) unsigned mc_open(int x,int y){return mc_bit(MC_WALK,x,y);}
static inline __attribute__((always_inline)) unsigned mc_door(int x,int y){return mc_bit(MC_DOOR,x,y);}
static UI_FN void mc_build(void){
 for(unsigned i=0;i<96*96/8;i++)MC_WALK[i]=MC_DOOR[i]=0;
 for(unsigned y=0;y<96;y++)for(unsigned x=0;x<96;x++){
  unsigned open=game.area?town_walkable(x,y):(x<80&&y<80&&game.map[y][x]);
  unsigned n=y*96+x;if(open)MC_WALK[n>>3]|=(uint8_t)(1u<<(n&7));
 }
 /* Door cells are passages in either state, as on the full map. */
 if(!game.area)for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&game.props[i];if(!game_prop_is_door(p->kind)||p->x>=80||p->y>=80)continue;
  unsigned n=p->y*96+p->x;MC_WALK[n>>3]|=(uint8_t)(1u<<(n&7));MC_DOOR[n>>3]|=(uint8_t)(1u<<(n&7));
 }
 /* The cathedral's down stairs sit below a one-cell notch in the wall. Open
  * that dead end (three closed sides) like a door. */
 if(!game.area&&(game.down_x||game.down_y))for(int y=game.down_y-1;y<=game.down_y+1;y++)for(int x=game.down_x-1;x<=game.down_x+1;x++){
  if(!mc_open(x,y))continue;
  unsigned closed=!mc_open(x-1,y)+!mc_open(x+1,y)+!mc_open(x,y-1)+!mc_open(x,y+1);
  if(closed>=3){unsigned n=(unsigned)y*96u+(unsigned)x;MC_DOOR[n>>3]|=(uint8_t)(1u<<(n&7));}
 }
 if(!game.area)game_explore_export(MS_FOG);
 map_contour_builds++;
}
/* A contour corner is lit when any of its four edges separates open and
 * closed cells with either side explored. */
static UI_FN unsigned mc_corner(int x,int y){
 /* Doors clear their four corners. */
 if(mc_door(x-1,y-1)||mc_door(x,y-1)||mc_door(x-1,y)||mc_door(x,y))return 0;
 unsigned a=mc_open(x-1,y-1),b=mc_open(x,y-1),c=mc_open(x-1,y),d=mc_open(x,y);
 if(a!=b&&(mc_seen(x-1,y-1)||mc_seen(x,y-1)))return 1;
 if(c!=d&&(mc_seen(x-1,y)||mc_seen(x,y)))return 1;
 if(a!=c&&(mc_seen(x-1,y-1)||mc_seen(x-1,y)))return 1;
 if(b!=d&&(mc_seen(x,y-1)||mc_seen(x,y)))return 1;
 return 0;
}
static inline __attribute__((always_inline)) unsigned mc_pixel(int p,int q){
 int sum=p+q;return sum&1?0:mc_corner(sum>>1,(q-p)>>1);
}
static inline __attribute__((always_inline)) void mc_put(unsigned i,unsigned j,unsigned on){
 uint32_t *w=&MC_ROWS[j][i>>5],bit=1u<<(i&31);if(on)*w|=bit;else *w&=~bit;
}
static UI_FN void mc_row(unsigned j){
 int p0=MS->x,q=MS->y+(int)j;uint32_t lo=0,hi=0;
 for(unsigned i=0;i<32;i++)lo|=mc_pixel(p0+(int)i,q)<<i;
 for(unsigned i=0;i<32;i++)hi|=mc_pixel(p0+32+(int)i,q)<<i;
 MC_ROWS[j][0]=lo;MC_ROWS[j][1]=hi;
}
/* Move the box origin by (dx,dy), |d|<=MC_STEP: shift kept pixels, then
 * evaluate only the entering rows and columns. */
static UI_FN void mc_shift(int dx,int dy){
 if(dy){
  int first=dy>0?0:MC_SIZE-1,last=dy>0?MC_SIZE:-1,step=dy>0?1:-1;
  for(int j=first;j!=last;j+=step){int from=j+dy;if(from>=0&&from<MC_SIZE){MC_ROWS[j][0]=MC_ROWS[from][0];MC_ROWS[j][1]=MC_ROWS[from][1];}}
 }
 if(dx)for(unsigned j=0;j<MC_SIZE;j++){
  uint32_t lo=MC_ROWS[j][0],hi=MC_ROWS[j][1];
  if(dx>0){lo=(lo>>dx)|(hi<<(32-dx));hi>>=dx;}else{int n=-dx;hi=(hi<<n)|(lo>>(32-n));lo<<=n;}
  MC_ROWS[j][0]=lo;MC_ROWS[j][1]=hi;
 }
 MS->x=(int16_t)(MS->x+dx);MS->y=(int16_t)(MS->y+dy);
 int top=dy>0?MC_SIZE-dy:0,bottom=dy>0?MC_SIZE:-dy;
 if(dy)for(int j=top;j<bottom;j++)mc_row((unsigned)j);
 int left=dx>0?MC_SIZE-dx:0,right=dx>0?MC_SIZE:-dx;
 if(dx)for(unsigned j=0;j<MC_SIZE;j++){
  if(dy&&(int)j>=top&&(int)j<bottom)continue;
  for(int i=left;i<right;i++)mc_put((unsigned)i,j,mc_pixel(MS->x+i,MS->y+(int)j));
 }
}
/* Newly explored blocks: refresh the box pixels they can change. Rows not
 * yet built pick up the change later. */
static UI_FN unsigned mc_reveal(void){
 if(game.area)return 0;
 uint8_t fog[200],change[200];game_explore_export(fog);unsigned touched=0;
 for(unsigned i=0;i<200;i++){change[i]=fog[i]^MS_FOG[i];MS_FOG[i]=fog[i];}
 for(unsigned i=0;i<200;i++)if(change[i])for(unsigned b=0;b<8;b++)if(change[i]&(1u<<b)){
  unsigned cell=i*8+b;int x=(int)(cell%40)*2,y=(int)(cell/40)*2;
  int left=x-y-2-MS->x,right=x-y+2-MS->x,top=x+y-MS->y,bottom=x+y+4-MS->y;
  if(left<0)left=0;if(top<0)top=0;if(right>MC_SIZE-1)right=MC_SIZE-1;if(bottom>(int)MS->cursor-1)bottom=(int)MS->cursor-1;
  for(int j=top;j<=bottom;j++)for(int i=left;i<=right;i++){mc_put((unsigned)i,(unsigned)j,mc_pixel(MS->x+i,MS->y+j));touched=1;}
 }
 return touched;
}
static UI_FN void mc_expand(void){
 static const uint16_t expand[16]={0,1,16,17,256,257,272,273,4096,4097,4112,4113,4352,4353,4368,4369};
 unsigned ink=map_line_color&15;uint32_t *out=MC_STAGE;
 for(unsigned ty=0;ty<8;ty++)for(unsigned tx=0;tx<8;tx++)for(unsigned r=0;r<8;r++){
  unsigned bits=(MC_ROWS[ty*8+r][tx>>2]>>((tx&3)*8))&255;
  *out++=(expand[bits&15]|((uint32_t)expand[bits>>4]<<16))*ink;
 }
}
/* Hero position in corner pixels, following the camera's sub-cell motion. */
static UI_FN void mc_hero(int *p,int *q){
 *p=map_round(cam_x+120-(game.area?1536:DUNGEON_ORIGIN),16);
 *q=map_round(cam_y+84-(game.area?128:72),8)+1;
}
static UI_FN void mc_draw(void){
 if(!map_visible||map_pending)return;
 int p,q;mc_hero(&p,&q);p-=MC_SIZE/2;q-=MC_SIZE/2;
 unsigned revision=game.area?0:game_explore_revision();
 if(map_last_x==-1||map_last_area!=game.area||!MS->corner){
  mc_build();MS->revision=revision;
  /* cursor: rows built; pixels: box changed since the last upload. */
  MS->x=(int16_t)p;MS->y=(int16_t)q;MS->cursor=MS->pixels=MS->ready=0;MS->corner=1;
  map_last_x=game.x;map_last_y=game.y;map_last_area=game.area;map_screen_fallback=map_screen_live=0;map_init=1;map_pending=3;return;
 }
 if(MS->revision!=revision){MS->revision=revision;if(mc_reveal())MS->pixels=1;}
 /* Build the box eight rows per frame while the map is still hidden. */
 if(MS->cursor<MC_SIZE){
  for(unsigned k=0;k<8&&MS->cursor<MC_SIZE;k++)mc_row(MS->cursor++);
  if(MS->cursor<MC_SIZE)return;
  MS->pixels=1;
 }
 int dx=p-MS->x,dy=q-MS->y;
 if(dx>MC_STEP||dx<-MC_STEP||dy>MC_STEP||dy<-MC_STEP){map_last_x=-1;return;}
 if(dx||dy){mc_shift(dx,dy);MS->pixels=1;}
 if(!MS->pixels)return;
 mc_expand();MS->pixels=0;MS->ready=1;
 map_last_x=game.x;map_last_y=game.y;map_last_revision=revision;map_unique_tiles=64;map_pending=2;
}
/* Fixed box: BG2 cells (0..7,0..7) hold the 64 corner tiles at the box. */
static UI_FN void mc_scroll(void){R16(0x04000018)=(uint16_t)((-MC_BOX_X)&511);R16(0x0400001a)=(uint16_t)((-MC_BOX_Y)&511);}
static UI_FN void mc_commit(void){
 if(map_pending==3){
  unsigned pal=(map_line_color>>4)<<12;
  for(unsigned k=0;k<64;k++)R16(0x0600e800u+(((k>>3)<<5)|(k&7))*2)=(uint16_t)(ms_tile(MC_TILE0+k)|pal);
 }else{
  dma(MC_STAGE,(void*)(0x06008000u+ms_tile(MC_TILE0)*32),MC_SIZE*MC_SIZE/2);
  if(MS->ready==1)map_init=0;
 }
}
/* Corner markers: box-relative positions, hidden outside the box. */
static inline int mc_in_box(int x,int y,int margin){return x>=MC_BOX_X+margin&&x<MC_BOX_X+MC_SIZE-margin&&y>=MC_BOX_Y+margin&&y<MC_BOX_Y+MC_SIZE-margin;}
static UI_FN void mc_markers(void){
 int ox=MC_BOX_X-MS->x,oy=MC_BOX_Y-MS->y;
 if(coop_active){
  for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){
   int nx=coop_players[i].x,ny=coop_players[i].y,x=ox+nx-ny,y=oy+nx+ny+1;
   if(!mc_in_box(x,y,2))continue;
   oam[i*4]=(uint16_t)((y-3)&255);oam[i*4+1]=(uint16_t)((x-3)&511);oam[i*4+2]=(uint16_t)((MAP_ARROW_TILE+i*8+(coop_players[i].facing&7))|(15<<12));
  }
 }else{
  int p,q;mc_hero(&p,&q);
  oam[0]=(uint16_t)((oy+q-3)&255);oam[1]=(uint16_t)((ox+p-3)&511);oam[2]=(uint16_t)((MAP_ARROW_TILE+(game.facing&7))|(15<<12));
 }
 if(game.area)for(int i=0;i<NPC_COUNT;i++){
  if(i==2&&!game_wounded_present())continue;
  int nx=npc_xy[i][0],ny=npc_xy[i][1],x=ox+nx-ny,y=oy+nx+ny+1;
  if(!mc_in_box(x,y,1))continue;
  int n=TOWN_MAP_NPC_OAM+i;
  oam[n*4]=(uint16_t)((y-3)&255);oam[n*4+1]=(uint16_t)((x-3)&511);oam[n*4+2]=(uint16_t)(996|(15<<12));
 }
}
