#ifndef ACTOR_PALETTE_H
#define ACTOR_PALETTE_H
#include <stdint.h>

/* A key below16 means the bank's normal scene palette. 16+b means a dark
 * duplicate of source bank b. Source banks and non-actor users stay reserved.
 * Keep scene palettes immutable: 8bpp deaths address all256 OBJ colors. */
typedef struct {uint8_t current[16],next[16];} ActorPaletteState;
#define ACTOR_PAL_INLINE static inline __attribute__((always_inline))
ACTOR_PAL_INLINE void actor_palette_state_reset(ActorPaletteState *s){
 for(unsigned b=0;b<16;b++)s->current[b]=s->next[b]=(uint8_t)b;
}
ACTOR_PAL_INLINE void actor_palette_state_plan(ActorPaletteState *s,unsigned reserved,unsigned dark){
 unsigned used=(reserved|dark)&65535u,pending=dark&65535u;
 for(unsigned b=0;b<16;b++)s->next[b]=(uint8_t)b;
 /* Preserve every surviving assignment before first-fit allocation: a new
  * family must not evict a bank belonging to a later surviving family. */
 for(unsigned b=0;b<16;b++)if(!(used&(1u<<b))){
  unsigned key=s->current[b];
  if(key>=16&&key<32&&(pending&(1u<<(key-16)))){
   s->next[b]=(uint8_t)key;used|=1u<<b;pending&=~(1u<<(key-16));
  }
 }
 for(unsigned source=0;source<16;source++)if(pending&(1u<<source))
  for(unsigned b=0;b<16;b++)if(!(used&(1u<<b))){
   s->next[b]=(uint8_t)(16+source);used|=1u<<b;break;
  }
}
ACTOR_PAL_INLINE unsigned actor_palette_state_bank(const ActorPaletteState *s,unsigned source){
 for(unsigned b=0;b<16;b++)if(s->next[b]==16+source)return b;
 return source;
}
ACTOR_PAL_INLINE int actor_palette_state_pending(const ActorPaletteState *s){
 for(unsigned b=0;b<16;b++)if(s->current[b]!=s->next[b])return 1;
 return 0;
}
ACTOR_PAL_INLINE uint16_t actor_palette_gamma(unsigned c,unsigned gamma){
 unsigned out=0;
 for(unsigned shift=0;shift<15;shift+=5){
  unsigned v=((c>>shift)&31)*(8+gamma)/10;if(v>31)v=31;out|=v<<shift;
 }
 return (uint16_t)out;
}
#undef ACTOR_PAL_INLINE
#endif
