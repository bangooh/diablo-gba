/* Pixel-positioned 5x5 HUD. Two 20-tile banks reuse the existing small font
 * cells. VCount uploads the next strip while the other bank is displayed;
 * no automap/actor tiles or DMA channels are borrowed. Menus stop the stream
 * before restoring their ordinary font. All packed state is double buffered. */
#include "../generated/info_font.h"
#include "info_font_metrics.h"
enum { INFO_STEP=7, INFO_WIDTH=160, INFO_HEIGHT=40 };
typedef struct {uint8_t pixels[1600],colors[40];uint16_t pairs[4][16];unsigned first;} InfoRaster;
static InfoRaster info_rasters[2] __attribute__((section(".iwram.info"),aligned(4)))={0};
static volatile unsigned info_stream_state __attribute__((section(".iwram.info")))=0;
static struct { char lines[5][23]; uint8_t count,style; } info_cache __attribute__((section(".iwram.info")))={0};
/* state: enabled bit0, live buffer bit1, pending bit2, pending buffer bit3. */
static UI_FN void info_stream_stop(void){
 info_stream_state=0;R16(0x04000200)&=(uint16_t)~4;R16(0x04000004)&=(uint16_t)~32;R16(0x04000202)=4;
}
static __attribute__((section(".iwram.coop"),noinline,minsize)) void info_stream_strip(unsigned row){
 const InfoRaster *r=&info_rasters[(info_stream_state>>1)&1];
 const uint8_t *pixels=r->pixels+row*320;
 unsigned bank=row&1;
 for(unsigned y=0;y<8;y++){
  const uint16_t *lut=r->pairs[r->colors[row*8+y]];
  const uint8_t *p=pixels+y*2;
  volatile uint32_t *dst=(volatile uint32_t*)(uintptr_t)(0x06008000+(97+bank*20)*64+y*8);
  for(unsigned col=0;col<20;col++){
   if(bank&&col==11)dst=(volatile uint32_t*)(uintptr_t)(0x0600c000+y*8);
   unsigned a=p[0],b=p[1];p+=16;
   dst[0]=lut[a&15]|((uint32_t)lut[a>>4]<<16);
   dst[1]=lut[b&15]|((uint32_t)lut[b>>4]<<16);dst+=16;
  }
 }
}
void UI_FN info_stream_irq(unsigned pending){
 if(!(info_stream_state&1))return;
 if(pending&1){
  unsigned s=info_stream_state;if(s&4)info_stream_state=1|((s>>2)&2);
  /* Only the gameplay BG1 mapping owns these cells. */
  if(!hud_transparent||(R16(0x04000000)&7)){info_stream_stop();return;}
  unsigned first=info_rasters[(info_stream_state>>1)&1].first;
  /* Publish the map together with its raster. Clearing live BG cells before
   * composing a new description made multi-frame co-op draws visibly blink. */
  if(s&4){
   volatile uint16_t *map=(volatile uint16_t*)0x0600d800;
   for(unsigned y=15;y<20;y++)for(unsigned x=0;x<30;x++)
    if(y==15||(x>=4&&x<26))map[y*32+x]=(uint16_t)(y>=15+first&&x>=5&&x<25?font_tile(((y-15)&1)*20+x-5):128);
  }
  /* Common one/two-line notices fit both resident banks. Leave them alone
   * on unchanged frames; only taller descriptions require scanline reuse. */
  if(first<5&&((s&4)||first<3)){info_stream_strip(first);if(first<4)info_stream_strip(first+1);}
  R16(0x04000200)&=(uint16_t)~4;R16(0x04000004)&=(uint16_t)~32;
  if(first<3){
   R16(0x04000004)=(uint16_t)((R16(0x04000004)&255)|32|((128+first*8)<<8));
   R16(0x04000202)=4;R16(0x04000200)|=4;
  }
 }else if(pending&4){
  unsigned y=R16(0x04000006),row=y>=144?4:y>=136?3:2;
  /* Arm the next compare before the bounded strip copy; its IRQ can latch
   * while this handler is still busy instead of scheduling a past line. */
  if(row==4){R16(0x04000200)&=(uint16_t)~4;R16(0x04000004)&=(uint16_t)~32;}
  else R16(0x04000004)=(uint16_t)((R16(0x04000004)&255)|((120+row*8)<<8));
  info_stream_strip(row);
 }
}
static inline __attribute__((always_inline)) void info_raster_pixel(InfoRaster *r,unsigned x,unsigned y,unsigned value){
 /* Store packed pixels in final tile order. The IRQ now streams contiguous
  * bytes and writes two aligned words per row, using the same two banks. */
 if(x>=160||y>=40)return;unsigned n=(y>>3)*320+(x>>3)*16+(y&7)*2+((x&7)>>2),shift=(x&3)*2;
 r->pixels[n]=(uint8_t)((r->pixels[n]&~(3u<<shift))|(value<<shift));
}
static inline __attribute__((always_inline)) unsigned info_glyph_width(unsigned ch,unsigned *left){
 if(ch>=128)ch='?';unsigned metric=info_font_metrics[ch];*left=metric&7;return metric>>3;
}
static UI_FN void info_raster_prepare(const char lines[5][23],unsigned count,unsigned style){
 /* Compare only visible bytes: info_reset deliberately leaves string tails. */
 unsigned same=(info_stream_state&1)&&info_cache.count==count&&info_cache.style==style;
 for(unsigned row=0;same&&row<count;row++)for(unsigned c=0;c<23;c++){
  if(info_cache.lines[row][c]!=lines[row][c]){same=0;break;}if(!lines[row][c])break;
 }
 if(same)return;
 unsigned old=info_stream_state;
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 unsigned back=((info_stream_state>>1)&1)^1;info_stream_state&=~4u;R16(0x04000208)=(uint16_t)ime;
 InfoRaster *r=&info_rasters[back];for(unsigned i=0;i<1600;i++)r->pixels[i]=0;
 r->first=(40-count*INFO_STEP)/8;
 for(unsigned i=0;i<40;i++)r->colors[i]=HUD_WHITE;
 /* Palette changes go through the menu/scene font reset, which stops us. */
 if(old&1){const InfoRaster *previous=&info_rasters[(old&4)?((old>>3)&1):((old>>1)&1)];
  for(unsigned c=0;c<4;c++)for(unsigned n=0;n<16;n++)r->pairs[c][n]=previous->pairs[c][n];
 }else{
 unsigned gray=info_color(8);
 for(unsigned c=0;c<4;c++){
  unsigned ink=hud_color_index(c),p[4]={0,gray,ink,ink};
  for(unsigned n=0;n<16;n++)r->pairs[c][n]=(uint16_t)(p[n&3]|p[n>>2]<<8);
 }
 }
 for(unsigned row=0;row<count;row++){
  unsigned n=0,width=0;while(lines[row][n]){unsigned left;width+=info_glyph_width((uint8_t)lines[row][n++],&left)+2;}
  unsigned x=(160-(width?width-2:0))/2,y=40-count*INFO_STEP+row*INFO_STEP;
  unsigned color=row<(style&7)?style>>3:HUD_WHITE;if(color>3)color=0;
  for(unsigned dy=0;dy<6;dy++)r->colors[y+dy]=(uint8_t)color;
  for(unsigned pass=0;pass<2;pass++){
   unsigned cursor=x;
   for(unsigned c=0;c<n;c++){
    unsigned ch=(uint8_t)lines[row][c],left,w=info_glyph_width(ch,&left);if(ch>=128)ch='?';
    for(unsigned gy=0;gy<5;gy++)for(unsigned gx=0;gx<w;gx++)if(info_font_rows[ch][gy]&(128u>>(left+gx)))
     info_raster_pixel(r,cursor+gx+(pass?0:1),y+gy+(pass?0:1),pass?2:1);
    cursor+=w+2;
   }
  }
 }
 for(unsigned row=0;row<count;row++)for(unsigned c=0;c<23;c++){info_cache.lines[row][c]=lines[row][c];if(!lines[row][c])break;}
 info_cache.count=(uint8_t)count;info_cache.style=(uint8_t)style;
 ime=R16(0x04000208);R16(0x04000208)=0;
 info_stream_state=(info_stream_state&2)|1|4|(back<<3);R16(0x04000208)=(uint16_t)ime;
}
