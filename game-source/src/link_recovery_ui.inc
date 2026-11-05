/* A reversible OBJ notice over the frozen game or options screen. Never
 * borrow a world buffer, alter a palette, or restart the game simulation.
 * Save only 48 currently unreferenced 4bpp tiles and twelve OAM entries. */
static struct {
 uint16_t pixels[12][64],attrs[12][3],tiles[12],slots[12],display;
 uint8_t active,bank,ink,paper,seconds,missing;
} recovery_ui __attribute__((section(".iwram.diag"),aligned(4)))={0};
#define RECOVERY_UI static UI_FN __attribute__((target("thumb"),minsize))
RECOVERY_UI void recovery_vblank(void){
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
}
RECOVERY_UI int recovery_prepare(void){
 uint8_t used[128]={0};unsigned found=0;
 for(unsigned i=0;i<128&&found<12;i++)if((R16(0x07000000+i*8)&0x300)==0x200)recovery_ui.slots[found++]=(uint16_t)i;
 if(found<12)return 0;
 static const uint8_t widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
 static const uint8_t heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
 unsigned display=R16(0x04000000);
 for(unsigned i=0;i<128;i++){
  unsigned a=R16(0x07000000+i*8),b=R16(0x07000002+i*8),c=R16(0x07000004+i*8);
  if((a&0x300)==0x200||(a>>14)==3)continue;
  unsigned w=widths[a>>14][b>>14]/8,h=heights[a>>14][b>>14]/8,scale=(a&0x2000)?2:1;
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*scale;x++){
   unsigned tile=((c&1023)+y*((display&64)?w*scale:32)+x)&1023;used[tile/8]|=(uint8_t)(1u<<(tile&7));
  }
 }
 found=0;
 for(unsigned at=0;at<=1020&&found<12;at++){
  unsigned free=1;for(unsigned n=0;n<4;n++)if(used[(at+n)/8]&(1u<<((at+n)&7)))free=0;
  if(!free)continue;
  recovery_ui.tiles[found++]=(uint16_t)at;
  for(unsigned n=0;n<4;n++)used[(at+n)/8]|=(uint8_t)(1u<<((at+n)&7));
 }
 if(found<12)return 0;
 unsigned contrast=0;
 for(unsigned bank=0;bank<16;bank++){
  unsigned lo=100,hi=0,paper=1,ink=1;
  for(unsigned n=1;n<16;n++){
   unsigned c=R16(0x05000200+bank*32+n*2),light=(c&31)+((c>>5)&31)+(c>>10);
   if(light<lo){lo=light;paper=n;}if(light>hi){hi=light;ink=n;}
  }
  if(hi-lo>contrast){contrast=hi-lo;recovery_ui.bank=(uint8_t)bank;recovery_ui.ink=(uint8_t)ink;recovery_ui.paper=(uint8_t)paper;}
 }
 if(!contrast)return 0;
 for(unsigned s=0;s<12;s++){
  for(unsigned n=0;n<64;n++)recovery_ui.pixels[s][n]=R16(0x06010000+recovery_ui.tiles[s]*32+n*2);
  for(unsigned n=0;n<3;n++)recovery_ui.attrs[s][n]=R16(0x07000000+recovery_ui.slots[s]*8+n*2);
 }
 recovery_ui.display=(uint16_t)display;recovery_ui.active=1;recovery_ui.seconds=255;return 1;
}
UI_FN __attribute__((target("thumb"),minsize)) void link_diag_wait(unsigned ticks,unsigned limit){
 if((R16(0x04000000)&7)||ticks>limit)return;
 /* Clients keep a short transport grace period; the visible policy is 30s. */
 unsigned visible=30u*16384u;
 unsigned seconds=ticks>=visible?0:(visible-ticks+16383u)/16384u;
 unsigned missing=coop_wire_missing();if(!missing)missing=link_diag.id?1:link_diag.members&~1u;
 if(!recovery_ui.active&&!recovery_prepare())return;
 if(recovery_ui.seconds==seconds&&recovery_ui.missing==missing)return;
 uint16_t pixels[768];unsigned paper=recovery_ui.paper,ink=recovery_ui.ink;
 for(unsigned n=0;n<768;n++)pixels[n]=(uint16_t)(paper*0x1111u);
 /* Natural player labels, centered with the existing 5x5 glyphs.
  * Trim empty glyph columns, retaining one pixel between letters and three
  * pixels for spaces. All player numbers fit in a 128x24-pixel notice. */
 char lines[3][29]={"CONNECTION LOST","WAITING FOR PLAYER ","30"};
 unsigned count=0;for(unsigned i=0;i<4;i++)if(missing&(1u<<i))count++;
 unsigned at=18;if(count>1){lines[1][at++]='S';lines[1][at++]=' ';}else at++;
 unsigned first=1;
 for(unsigned i=0;i<4;i++)if(missing&(1u<<i)){
  if(!first){lines[1][at++]=',';lines[1][at++]=' ';}first=0;
  lines[1][at++]='1'+i;
 }
 lines[1][at]=0;lines[2][0]='0'+seconds/10;lines[2][1]='0'+seconds%10;
 for(unsigned row=0;row<3;row++){
  uint8_t offsets[29],widths[29];unsigned width=0;
  for(unsigned c=0;lines[row][c];c++){
   unsigned bits=0;for(unsigned y=0;y<5;y++)bits|=info_font_rows[(unsigned)lines[row][c]][y];
   unsigned lo=0,hi=4;
   if(bits){while(!(bits&(128u>>lo)))lo++;while(!(bits&(128u>>hi)))hi--;}
   offsets[c]=(uint8_t)lo;widths[c]=(uint8_t)(bits?hi-lo+1:2);width+=widths[c]+1;
  }
  if(width)width--;unsigned pen=(128-width)/2;
  for(unsigned c=0;lines[row][c];c++){
   for(unsigned y=0;y<5;y++)for(unsigned x=0;x<widths[c];x++)
    if(info_font_rows[(unsigned)lines[row][c]][y]&(128u>>(x+offsets[c]))){
     unsigned px=pen+x,index=row*256+(px/8)*16+(y+1)*2+(px&7)/4,shift=(px&3)*4;
     pixels[index]=(uint16_t)((pixels[index]&~(15u<<shift))|(ink<<shift));
    }
   pen+=widths[c]+1;
  }
 }
 recovery_vblank();
 for(unsigned s=0;s<12;s++){
  for(unsigned n=0;n<64;n++)R16(0x06010000+recovery_ui.tiles[s]*32+n*2)=pixels[s*64+n];
  unsigned at=0x07000000+recovery_ui.slots[s]*8;
  R16(at)=(68+(s/4)*8)|0x4000;R16(at+2)=(56+(s%4)*32)|0x4000;R16(at+4)=(uint16_t)(recovery_ui.tiles[s]|recovery_ui.bank<<12);
 }
 R16(0x04000000)|=0x1000;recovery_ui.seconds=(uint8_t)seconds;recovery_ui.missing=(uint8_t)missing;
}
UI_FN __attribute__((target("thumb"),minsize)) void link_diag_wait_end(void){
 if(!recovery_ui.active)return;
 recovery_vblank();
 for(unsigned s=0;s<12;s++){
  for(unsigned n=0;n<64;n++)R16(0x06010000+recovery_ui.tiles[s]*32+n*2)=recovery_ui.pixels[s][n];
  for(unsigned n=0;n<3;n++)R16(0x07000000+recovery_ui.slots[s]*8+n*2)=recovery_ui.attrs[s][n];
 }
 R16(0x04000000)=recovery_ui.display;recovery_ui.active=0;
}
#undef RECOVERY_UI
