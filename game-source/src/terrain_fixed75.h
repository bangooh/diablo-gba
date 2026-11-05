/* Eight-aligned wall tiles: ROM masks preserve the opaque ground diamond.
 * Upper rows use the constant75% pattern. Colors and occlusion share masks. */
static const uint32_t terrain_ground_holes[8][8]={
 {0xff00ff00u,0xff00ff00u,0xff00ff00u,0xff00ff00u,0xff00ff00u,0x00000000u,0x00000000u,0x00000000u},
 {0xff00ff00u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u},
 {0x00000000u,0xff00ff00u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u},
 {0xff00ff00u,0xff00ff00u,0xff00ff00u,0xff00ff00u,0x00000000u,0xff00ff00u,0x00000000u,0x00000000u},
 {0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u},
 {0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u},
 {0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u},
 {0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u}
};
static UI_FN void terrain_fixed75(uint8_t *pixels,uint8_t *depth,
 const uint8_t *piece,unsigned z,int dx,int dy,unsigned height){
 uint32_t *out=(uint32_t*)pixels,*dst=(uint32_t*)depth;
 const uint32_t *src=(const uint32_t*)piece;uint32_t zz=z*0x01010101u;
 int bottom=dy-(int)height+16;
 const uint32_t *ground=bottom>=0?terrain_ground_holes[(bottom>>3)*4+(dx>>3)]:0;
 for(unsigned y=0;y<8;y++)for(unsigned half=0;half<2;half++){
  unsigned i=y*2+half;uint32_t v=src[i];
  if(y&1)v&=~(ground?ground[(y>>1)*2+half]:0xff00ff00u);
  if(!v)continue;
  uint32_t mask=(((((v&0x7f7f7f7fu)+0x7f7f7f7fu)|v)&0x80808080u)>>7)*255u;
  out[i]=(out[i]&~mask)|v;dst[i]=(dst[i]&~mask)|(zz&mask);
 }
}
