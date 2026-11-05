/* Retail DrawView: RedBack before gmenu; bottom panel keeps its palette.
 * GBA tiles no longer contain original palette indices, so recover the nearest
 * source index before applying the exact retail translation. Cold death path.
 * Dynamic background slots are safe: all HUD/font/globe colors are static. */
#include "../generated/death_red.h"
static UI_FN unsigned death_source_index(unsigned color,const uint16_t *palette){
 unsigned best=~0u,index=0;
 for(unsigned i=0;i<256;i++){
  unsigned p=palette[i];int r=(int)(color&31)-(int)(p&31),g=(int)((color>>5)&31)-(int)((p>>5)&31),b=(int)((color>>10)&31)-(int)((p>>10)&31);
  unsigned d=r*r+g*g+b*b;if(d<best){best=d;index=i;}
 }
 return index;
}
static UI_FN __attribute__((target("thumb"),minsize)) void death_red_back(void){
 if(game.area)return;
 unsigned region=dungeon_level<=4?0:dungeon_level<=8?1:dungeon_level<=12?2:3;
 const uint16_t *palette=death_source_palette[region];
 uint8_t slots[16],map[256];unsigned count=0;
 for(unsigned i=1;i<256&&count<16;i++)if(!bg_static_index(i)&&!(region==3&&death_source_index(base_palette[i],palette)<32))slots[count++]=i;
 if(count<16)return;
 for(unsigned i=0;i<16;i++){
  unsigned c=i?palette[223+i]:0,out=0;
  for(unsigned shift=0;shift<15;shift+=5){unsigned v=((c>>shift)&31)*(8+gamma_level)/10;out|=(v>31?31:v)<<shift;}
  R16(0x05000000+slots[i]*2)=out;
 }
 if(!death_red_ready){
  for(unsigned i=0;i<256;i++){
   unsigned source=death_source_index(base_palette[i],palette),red=death_red_translation[source];
   /* Hell keeps the original first32 cycling colors in RedBack. */
   map[i]=region==3&&source<32?i:slots[red?red-223:0];
  }
  map[0]=0;
  /* Include the43 scattered terrain cells. Entering a menu invalidates
   * their ring keys but leaves their pixels displayed beneath the overlay.
   * world_tile skips the dedicated font/globe cells. */
  for(unsigned tile=0;tile<WORLD_TILES;tile++){
   volatile uint16_t *p=(volatile uint16_t*)(0x06000000+world_tile(tile)*64);
   for(unsigned i=0;i<32;i++){unsigned v=p[i];p[i]=map[v&255]|(map[v>>8]<<8);}
  }
  death_red_ready=1;
 }
 /* World OBJ banks only. HUD/belt uses bank5; world sprites begin at42. */
 unsigned banks=0;
 for(unsigned i=42;i<126;i++)if(!(R16(0x07000000+i*8)&0x200))banks|=(R16(0x07000000+i*8)&0x2000)?65535u:1u<<(R16(0x07000000+i*8+4)>>12);
 for(unsigned bank=0;bank<16;bank++)if((banks&(1u<<bank))&&bank!=5){
  unsigned sourcebank=actor_palettes.current[bank]>=16?actor_palettes.current[bank]-16:bank;
  for(unsigned i=1;i<16;i++){
   unsigned c=base_palette[256+sourcebank*16+i],index=death_source_index(c,palette);
   if(region!=3||index>=32)c=palette[death_red_translation[index]];
   unsigned out=0;for(unsigned shift=0;shift<15;shift+=5){unsigned v=((c>>shift)&31)*(8+gamma_level)/10;out|=(v>31?31:v)<<shift;}
   R16(0x05000200+bank*32+i*2)=out;
  }
 }
}
