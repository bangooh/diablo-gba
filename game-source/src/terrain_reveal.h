/* Compose the existing hero reveal window in packed words. Input/output
 * buffers are word aligned, as in terrain_piece. Window bounds are tile-local.
 * Holes retain BOTH the previous color and depth (including rear walls).
 * World tiles begin on multiples of eight, so local parity is world parity. */
static UI_FN void terrain_reveal(uint8_t *pixels,uint8_t *depth,
 const uint8_t *piece,unsigned z,int left,int right,int top,int bottom){
 uint32_t *out=(uint32_t*)pixels,*dst=(uint32_t*)depth;
 const uint32_t *src=(const uint32_t*)piece;
 uint32_t window[2]={0,0},zz=z*0x01010101u;
 for(int x=0;x<8;x++)if(x>=left&&x<right)window[x>>2]|=255u<<((x&3)*8);
 for(int y=0;y<8;y++){
  uint32_t holes=(y&1)?0x00ff00ffu:0xff00ff00u;
  unsigned inside=y>=top&&y<bottom;
  for(unsigned half=0;half<2;half++){
   unsigned i=(unsigned)y*2+half;uint32_t v=src[i];
   if(inside)v&=~(window[half]&holes);
   if(!v)continue;
   /* Independent per-byte nonzero flags; no borrow crosses byte lanes. */
   uint32_t mask=(((((v&0x7f7f7f7fu)+0x7f7f7f7fu)|v)&0x80808080u)>>7)*255u;
   out[i]=(out[i]&~mask)|v;dst[i]=(dst[i]&~mask)|(zz&mask);
  }
 }
}
