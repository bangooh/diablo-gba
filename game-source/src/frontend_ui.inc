/* Stage81. Mode4 CPU rendering shared by the frontend and host previews. */
#include "../generated/frontend_stage81.h"
static UI_FN unsigned front_black(void){
 for(unsigned i=1;i<252;i++)if(!base_palette[i])return i;
 return 0;
}
static UI_FN unsigned front_gray(void){
 unsigned best=~0u,index=1;
 for(unsigned i=1;i<252;i++){unsigned c=base_palette[i];int r=(c&31)-6,g=((c>>5)&31)-6,b=((c>>10)&31)-6;unsigned d=r*r+g*g+b*b;if(d<best){best=d;index=i;}}
 return index;
}
static UI_FN void front_tiny(int x,int y,const char *text,unsigned color){
 uint8_t *dst=(uint8_t*)town_map;const char *letters=OVERLAY_GLYPHS;
 for(unsigned pass=0;pass<2;pass++){
  int pen=x,offset=pass?0:1;
  for(const char *s=text;*s;s++,pen+=4){
   unsigned ch=(uint8_t)*s,n=0;while(letters[n]&&letters[n]!=ch)n++;
   uint8_t rows[5];
   for(unsigned yy=0;yy<5;yy++)rows[yy]=(uint8_t)(ch=='.'?(yy==4?2:0):ch=='-'?(yy==2?7:0):ch=='v'?(yy<2?0:yy<4?5:2):letters[n]?overlay_rows[n][yy]:0);
   for(unsigned yy=0;yy<5;yy++){
    for(unsigned xx=0;xx<3;xx++){
     int px=pen+(int)xx+offset,py=y+(int)yy+offset;
     unsigned bit=4u>>xx;
     if((rows[yy]&bit)&&px>=0&&px<240&&py>=0&&py<160)
      dst[py*240+px]=(uint8_t)(pass?((color==253||color==254)?front_small_ink(yy?rows[yy-1]:0,yy<4?rows[yy+1]:0,bit):color):91);
    }
   }
  }
 }
}
static UI_FN void front_focus(int y,int left,int right){
 _Static_assert(FRONT_FOCUS_WIDTH==10&&FRONT_FOCUS_HEIGHT==10&&sizeof(front_focus81[0])==100,"menu focus must be 10x10");
 uint8_t *dst=(uint8_t*)town_map;const uint8_t *frame=front_focus81[frontend_frame&7];
 /* Resample from the original square42px artwork, not the former8x14 copy. */
 for(int yy=0;yy<10;yy++)for(int xx=0;xx<10;xx++){
  unsigned c=frame[yy*FRONT_FOCUS_WIDTH+xx];int py=y+yy;
  if(!c||py<0||py>=160)continue;
  if(left+xx>=0&&left+xx<240)dst[py*240+left+xx]=(uint8_t)c;
  if(right+xx>=0&&right+xx<240)dst[py*240+right+xx]=(uint8_t)c;
 }
}
static UI_FN void front_mark(int y){front_focus(y-1,43,187);}
static UI_FN void front_main_screen(unsigned choice){
 const char *items[]={"SINGLE PLAYER","MULTI PLAYER","CREDITS","EXIT DIABLO"};
 front_tiny(5,151,"V 0.9.5",253);
 for(unsigned i=0;i<4;i++)front_text(76+i*16,items[i],i==choice?254:253);
 front_mark(76+choice*16);
}
static UI_FN __attribute__((target("thumb"),minsize)) void front_hero_screen(unsigned mode,unsigned choice,int saved,unsigned saved_class,const HeroProgress *saved_progress){
 unsigned cls=mode==10?choice:!choice&&saved?saved_class:3;
 /* Cache the static panel and lazily decoded, scaled logo frames in the
  * suspended scene arena. Steady selection never decodes full screens. */
 uint8_t *dst=(uint8_t*)town_map;
 uint32_t *cached=(uint32_t*)(dst+151680),*ready=cached+1;
 if(*cached!=cls){lz_block(asset_hero_choices,cls,dst,38400);dma(dst+11520,dst+38400,26880);*cached=cls;}
 unsigned frame=frontend_frame%15;uint8_t *logo=dst+65280+frame*5760;
 if(!(*ready&(1u<<frame))){
  lz_block(asset_front_screens,15+frame,dst,38400);
  for(unsigned y=0;y<48;y++)for(unsigned x=0;x<120;x++)logo[y*120+x]=dst[(y*4/3)*240+45+x*5/4];
  *ready|=1u<<frame;
 }
 /* Palette index0 is yellow in the shared frontend palette, not black. */
 unsigned black=front_black();
 for(unsigned i=0;i<11520/4;i++)((uint32_t*)dst)[i]=black*0x01010101u;
 dma(dst+38400,dst+11520,26880);
 for(unsigned y=0;y<48;y++)dma(logo+y*120,dst+y*240+60,120);
 front_text(54,"SINGLE PLAYER CHARACTERS",254);
 front_text_at(160,73,mode==10?"CHOOSE CLASS":"SELECT HERO",253);
 const char *classes[]={"WARRIOR","ROGUE","SORCERER"};
 if(mode==10){for(unsigned i=0;i<3;i++)front_text_at(160,94+i*15,classes[i],i==choice?254:253);front_focus(93+choice*15,96,215);}
 else{
  if(saved)front_text_at(160,94,"SAVED HERO",!choice?254:253);
  unsigned y=saved?112:94;front_text_at(160,y,"NEW HERO",choice?254:253);front_focus((choice?y:94)-1,96,215);
 }
 HeroProgress stats;if(cls<3)game_progress_init(&stats,cls);
 const HeroProgress *p=mode==7&&!choice&&saved?saved_progress:&stats;
 const char *labels[]={"LEVEL","STR","MAG","DEX","VIT"};
 for(unsigned i=0;i<5;i++){
  unsigned y=i?107+i*9:104;front_tiny(19,y,labels[i],253);
  if(cls==3){front_tiny(63,y,"--",253);continue;}
  unsigned v=i==0?p->level:i==1?p->strength:i==2?p->magic:i==3?p->dexterity:p->vitality;
  char value[4];unsigned n=0;if(v>=100)value[n++]='0'+v/100;if(v>=10)value[n++]='0'+v/10%10;value[n++]='0'+v%10;value[n]=0;
  front_tiny(71-(int)n*4,y,value,253);
 }
 front_text_at(160,145,"B: CANCEL     A: OK",253);
}
static UI_FN void front_credit_text(int y,const char *text,unsigned color){
 unsigned width=0;for(const char *s=text;*s;s++){unsigned left;width+=dialogue_glyph((uint8_t)*s,&left)+1;}
 int x=(230-(int)width)/2;uint8_t *dst=(uint8_t*)town_map;
 for(;*text;text++){
  unsigned ch=(uint8_t)*text,left,w=dialogue_glyph(ch,&left);if(ch<32||ch>=128)ch='?';
  for(unsigned yy=0;yy<7;yy++)for(unsigned xx=0;xx<w;xx++)if((town_dialogue_font[ch-32][yy]&(128u>>(left+xx)))&&x+(int)xx>=0&&x+(int)xx<230&&y+(int)yy>=26&&y+(int)yy<153)dst[(y+yy)*240+x+xx]=(uint8_t)color;
  x+=(int)w+1;
 }
}
enum { FRONT_CREDIT_ROWS=10 };
static UI_FN void front_credit_screen(unsigned scroll){
 lz_decode(asset_front_credits,town_map,sizeof(town_map));front_text(8,"CREDITS",254);
 for(unsigned row=0;row<FRONT_CREDIT_ROWS&&scroll+row<FRONT_CREDIT_LINES;row++)front_credit_text(30+row*12,front_credits81[scroll+row].text,front_credits81[scroll+row].color);
 uint8_t *dst=(uint8_t*)town_map;unsigned track=120,thumb=track*FRONT_CREDIT_ROWS/FRONT_CREDIT_LINES;if(thumb<6)thumb=6;
 unsigned top=30+(track-thumb)*scroll/(FRONT_CREDIT_LINES-FRONT_CREDIT_ROWS);
 unsigned black=front_gray();
 for(unsigned y=30;y<150;y++){dst[y*240+234]=(uint8_t)black;dst[y*240+235]=(uint8_t)black;}
 /* Two-pixel thumb; frontend palette matches of the dialogue frame's
  * original Town.PAL195 highlight and198 edge. */
 for(unsigned y=top;y<top+thumb;y++){dst[y*240+234]=y==top+thumb-1?22:10;dst[y*240+235]=y==top?10:22;}
}

static UI_FN void front_difficulty_screen(unsigned choice){
 front_text(80,"SELECT DIFFICULTY",253);
 for(unsigned i=0;i<GAME_DIFFICULTY_COUNT;i++)front_text(100+i*14,game_difficulty_name(i),i==choice?254:253);
 front_mark(100+choice*14);front_text(145,"B: CANCEL   A: OK",253);
}
