/* Two-level terrain lighting. Both hardware windows have the same bright
 * policy; outside them the BG0 color effect applies. Tables live in the
 * dungeon scratch arena, not newly allocated EWRAM. Included after lighting.inc.
 * Keep every source on the hardware path. If a row has more than two bright
 * intervals, preserve its largest dark gap and fill the smaller gaps. This
 * is the two-window superset with the fewest extra bright pixels. */
#ifndef LIGHT_HARDWARE
#define LIGHT_HARDWARE 1
#endif
#ifndef LIGHT_WINDOW_TABLES
#error "LIGHT_WINDOW_TABLES must name two word-aligned uint32_t[161] RAM tables"
#endif
static const uint32_t *volatile light_window_live;
static struct {
 uint8_t requested,back,valid,count;
} light_window_state;
static struct {int16_t x,y;uint8_t radius,pad;} light_window_previous[LIGHT_CAPACITY];

static UI_FN unsigned lighting_window_sources(uint8_t active[LIGHT_CAPACITY]){
 /* A screen-external ellipse cannot contribute. A continuously contained
  * ellipse cannot add any integer pixel either. Reject both before scanning
  * 160 rows, retaining the first of coincident equal-radius sources. */
 unsigned active_count=0;
 for(unsigned i=0;i<light_count;i++){
  const LightSource *s=&light_sources[i];unsigned rx=s->radius*8,ry=s->radius*5;
  if(s->radius>15)continue;
  int x=s->x-cam_x,y=s->y-cam_y;
  if(x+(int)rx<0||x-(int)rx>=240||y+(int)ry<0||y-(int)ry>=160)continue;
  unsigned contained=0;
  for(unsigned j=0;j<light_count;j++){
   const LightSource *other=&light_sources[j];
   if(i==j||other->radius>15||other->radius<s->radius||(other->radius==s->radius&&j>i))continue;
   int reach=(int)other->radius-s->radius;
   int dx=(int)s->x-other->x,dy=(int)s->y-other->y;
   /* Bounding checks also keep the squares safe for int16_t world extremes. */
   if(dx>reach*8||dx<-reach*8||dy>reach*5||dy<-reach*5)continue;
   if(25*dx*dx+64*dy*dy<=1600*reach*reach){contained=1;break;}
  }
  if(!contained)active[active_count++]=(uint8_t)i;
 }
 return active_count;
}
static UI_FN void lighting_window_rows(uint32_t *table,const uint8_t active[LIGHT_CAPACITY],unsigned active_count,int first,int last){
 for(int y=first;y<=last;y++){
  uint16_t left[LIGHT_CAPACITY],right[LIGHT_CAPACITY];unsigned count=0;
  for(unsigned i=0;i<active_count;i++){
   const LightSource *s=&light_sources[active[i]];int dy=cam_y+y-s->y;
   if(dy<0)dy=-dy;if(dy>75||s->radius>15)continue;
   unsigned span=light_spans[s->radius][dy];if(span==255)continue;
   int x=s->x-cam_x,l=x-(int)span,r=x+(int)span+1;
   if(r<=0||l>=240)continue;if(l<0)l=0;if(r>240)r=240;
   unsigned j=count++;
   while(j&&left[j-1]>l){left[j]=left[j-1];right[j]=right[j-1];j--;}
   left[j]=(uint16_t)l;right[j]=(uint16_t)r;
  }
  unsigned windows=0;
  for(unsigned i=0;i<count;){
   unsigned l=left[i],r=right[i++];
   while(i<count&&left[i]<=r){if(right[i]>r)r=right[i];i++;}
   left[windows]=(uint16_t)l;right[windows++]=(uint16_t)r;
  }
  uint32_t packed=0;
  if(windows){
   unsigned split=0,gap=0;
   for(unsigned i=1;i<windows;i++)if((unsigned)(left[i]-right[i-1])>gap){gap=left[i]-right[i-1];split=i;}
   packed=(left[0]<<8)|right[split?split-1:0];
   if(split)packed|=((uint32_t)(left[split]<<8)|right[windows-1])<<16;
  }
  table[y]=packed;
 }
 /* Visible HBlank159 installs line0 before VBlank. The IRQ resets the source
  * before every hardware frame, including frames with no completed draw. */
 table[160]=table[0];
}
static UI_FN unsigned lighting_window_build(uint32_t *table){
 uint8_t active[LIGHT_CAPACITY];unsigned count=lighting_window_sources(active);
 lighting_window_rows(table,active,count,0,159);return 1;
}

static UI_FN void lighting_hardware_prepare(void){
 /* Opaque BG2 map lines need no alpha effect. Keep the terrain hardware
  * brightness windows active whether the automap is open or closed. */
 if(!LIGHT_HARDWARE||game.area||coop_active||!light_count){light_window_state.requested=0;light_window_state.valid=0;return;}
 /* Keep all world sources for actor lighting. Only ellipses contributing to
  * this screen belong to the hardware cache key: offscreen/contained missiles
  * cannot invalidate an unchanged hero-only window. Screen coordinates also
  * preserve the centered hero's table while the camera follows movement. */
 uint8_t active[LIGHT_CAPACITY];unsigned count=lighting_window_sources(active);
 unsigned previous=light_window_state.count,slots=count>previous?count:previous;
 int first=160,last=-1;
 if(!light_window_state.valid||!light_window_live){first=0;last=159;previous=0;slots=count;}
 for(unsigned i=0;i<slots;i++){
  int x=0,y=0;unsigned radius=0;
  if(i<count){const LightSource *s=&light_sources[active[i]];x=s->x-cam_x;y=s->y-cam_y;radius=s->radius;}
  if(i>=previous||i>=count||light_window_previous[i].x!=x||light_window_previous[i].y!=y||light_window_previous[i].radius!=radius){
   /* A row depends only on lights crossing it. Rebuild the union of old/new
    * vertical extents; all other rows can be copied from the live table. */
   if(i<previous){int top=light_window_previous[i].y-(int)light_window_previous[i].radius*5,bottom=light_window_previous[i].y+(int)light_window_previous[i].radius*5;if(top<first)first=top;if(bottom>last)last=bottom;}
   if(i<count){int top=y-(int)radius*5,bottom=y+(int)radius*5;if(top<first)first=top;if(bottom>last)last=bottom;}
  }
  if(i<count){light_window_previous[i].x=(int16_t)x;light_window_previous[i].y=(int16_t)y;light_window_previous[i].radius=(uint8_t)radius;}
 }
 light_window_state.count=(uint8_t)count;
 if(first>last)return;
 if(first<0)first=0;if(last>159)last=159;
 unsigned back=light_window_live==LIGHT_WINDOW_TABLES[0]?1:0;
 light_window_state.back=(uint8_t)back;
 if(first==0&&last==159)light_window_state.requested=(uint8_t)lighting_window_build(LIGHT_WINDOW_TABLES[back]);
 else{
  uint32_t *table=LIGHT_WINDOW_TABLES[back];const uint32_t *live=light_window_live;
  for(unsigned y=0;y<161;y++)table[y]=live[y];
  lighting_window_rows(table,active,count,first,last);light_window_state.requested=1;
 }
 light_window_state.valid=1;
}
static UI_FN unsigned lighting_hardware_active(void){return light_window_state.requested;}

/* Called by audio.c on every VBlank. DMA0 is unused by audio/DMA3 copies and
 * can read this RAM table. It cannot read the original GamePak span table.
 * Also safe at draw completion: start with the actual scanline and transfer
 * only its successors until the next VBlank resets the source to line1. */
void UI_FN lighting_hardware_vblank_irq(void){
 const uint32_t *table=light_window_live;
 R32(0x040000b8)=0;if(!table)return;
 unsigned y=R16(0x04000006);if(y>=160)y=0;
 R32(0x04000040)=table[y];R32(0x04000044)=0x00a000a0;
 R32(0x040000b0)=(uint32_t)(uintptr_t)(table+y+1);
 R32(0x040000b4)=0x04000040;
 /* Enable, HBlank, repeat, 32-bit, fixed destination, one word. */
 R32(0x040000b8)=0xa6400001u;
}
static UI_FN void lighting_window_disable(void){
 light_window_live=0;R32(0x040000b8)=0;
 R32(0x04000040)=240;R32(0x04000044)=160;
 R16(0x04000048)=0x0037;R16(0x0400004a)=0x0032;
}
static UI_FN void lighting_hardware_commit(void){
 const uint32_t *table=light_window_state.requested?LIGHT_WINDOW_TABLES[light_window_state.back]:0;
 if(table==light_window_live)return;
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 __asm__ volatile("" ::: "memory");
 if(table){light_window_live=table;lighting_hardware_vblank_irq();}
 else lighting_window_disable();
 R16(0x04000208)=(uint16_t)ime;
}
static UI_FN void lighting_hardware_stop(void){
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 lighting_window_disable();light_window_state.requested=0;light_window_state.valid=0;
 R16(0x04000208)=(uint16_t)ime;
}
