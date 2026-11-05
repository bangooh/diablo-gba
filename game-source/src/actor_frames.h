#ifndef ACTOR_FRAMES_H
#define ACTOR_FRAMES_H
#include <stdint.h>
/* dst and scratch are 4-byte aligned; scratch is a separate 2048-byte
 * renderer workspace, not a persistent frame cache. */
unsigned actor_frame(const uint8_t *clip,unsigned index,void *dst,unsigned capacity,
                     void *scratch,const uint8_t *pool);
unsigned actor_frame_external(const uint8_t *clip,unsigned index,void *dst,unsigned capacity,void *scratch,const uint8_t *pool,const uint8_t *external_pool);
/* Validated SPR2 tile presence, before expanding its sparse record. Other
 * formats return zero; callers retain their ordinary full-frame path. */
unsigned actor_frame_tiles(const uint8_t *clip,unsigned index,uint32_t tiles[2]);
#endif
