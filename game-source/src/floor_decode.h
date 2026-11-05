/* Decode to a word-aligned transparent 64x64 canvas. */
/* D8: decode settled floor decals in ROM; reserve IWRAM for VBlank uploads. */
static UI_FN void floor_decode(const uint8_t *asset,unsigned frame,uint8_t *out){
 for(unsigned i=0;i<1024;i++)((uint32_t*)out)[i]=0;
 const uint8_t *picture=asset+((const uint32_t*)asset)[frame+1];
 const uint8_t *body=asset+*(const uint32_t*)(picture+8),*palette=asset+*(const uint32_t*)(picture+4);
 const uint16_t *rows=(const uint16_t*)body;
 unsigned bits=picture[2],mask=(1u<<bits)-1;
 for(unsigned row=0;row<picture[1];row++){
  const uint8_t *p=body+rows[row]-8;unsigned col=0;uint8_t *dst=out+(row+picture[0])*64;
  for(unsigned run;(run=*p++);){
   if(run>=128){col+=256-run;continue;}
   unsigned repeat=run>64;if(repeat)run-=64;
   for(unsigned i=0;i<run;i++){
    unsigned bit=i*bits,v=repeat?p[0]:p[bit>>3]>>(bit&7);
    if(!repeat&&(bit&7)+bits>8)v|=(unsigned)p[(bit>>3)+1]<<(8-(bit&7));
    dst[col+i]=palette[v&mask];
   }
   col+=run;p+=repeat?1:(run*bits+7)/8;
  }
 }
}
