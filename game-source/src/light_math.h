#ifndef LIGHT_MATH_H
#define LIGHT_MATH_H
#include <stdint.h>
/* Four players + finale + eight missiles, or hero + four candles + missiles.
 * World pixel coordinates fit in int16_t; no new full-screen mask in RAM. */
/* Outside brightness75%: hardware darkness4/16. */
enum { LIGHT_CAPACITY=13, LIGHT_OUTSIDE_BLDY=4 };
typedef struct {int16_t x,y;uint8_t radius,flat;} LightSource;
/* Presentation-only ellipse: radius10 is80x50 screen pixels. Equipment and
 * exploration keep their original radius units. Bound before squaring so
 * differences between int16_t world extremes cannot overflow. */
static unsigned light_band(int dx,int dy,unsigned radius){
 if(radius>15)return 3;
 int rx=(int)radius*8,ry=(int)radius*5;
 if(dx<-rx||dx>rx||dy<-ry||dy>ry)return 3;
 return (unsigned)(25*dx*dx+64*dy*dy)<=1600*radius*radius?0:3;
}
/* Never select transparent index0 for a solid actor pixel. Match the scene's
 * existing palette; no extra palette banks and no effect on UI/spell pixels. */
static unsigned light_color_masked(const uint16_t *palette,unsigned count,unsigned index,unsigned band,const uint8_t *allowed){
 if(!index||!band)return index;
 unsigned c=palette[index],scale=16-LIGHT_OUTSIDE_BLDY,best=~0u,result=index;
 /* Match hardware BLDY4's nominal75% outside intensity. */
 int r=(c&31)*scale/16,g=((c>>5)&31)*scale/16,b=((c>>10)&31)*scale/16;
 unsigned ceiling=(c&31)+((c>>5)&31)+((c>>10)&31);
 for(unsigned i=1;i<count;i++){
  if(allowed&&!(allowed[i>>3]&(1u<<(i&7))))continue;
  unsigned v=palette[i];int vr=v&31,vg=(v>>5)&31,vb=(v>>10)&31;
  if((unsigned)(vr+vg+vb)>ceiling)continue;
  int dr=vr-r,dg=vg-g,db=vb-b;unsigned distance=dr*dr+dg*dg+db*db;
  if(distance<best){best=distance;result=i;}
 }
 return result;
}
static unsigned light_color(const uint16_t *palette,unsigned count,unsigned index,unsigned band){
 return light_color_masked(palette,count,index,band,0);
}
#endif
