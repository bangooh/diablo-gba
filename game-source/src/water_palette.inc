/* Only the dedicated L3 slots change. BG pixels and depth remain immutable
 * throughout native purification; other scene art never references these. */
static unsigned water_palette_seen=255;
volatile unsigned water_palette_writes;
static UI_FN void water_palette_apply(unsigned state){
 if(state==water_palette_seen)return;
 water_palette_seen=state;
 for(unsigned n=0;n<30;n++){
  unsigned i=bg_water_slots[n],c=bg_water_color(state,n);
  if(base_palette[i]==c)continue;
  base_palette[i]=(uint16_t)c;
  unsigned out=0;
  for(unsigned shift=0;shift<15;shift+=5){
   unsigned v=((c>>shift)&31)*(8+gamma_level)/10;
   if(v>31)v=31;out|=v<<shift;
  }
  R16(0x05000000+i*2)=(uint16_t)out;water_palette_writes++;
 }
}
