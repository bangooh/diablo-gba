#ifndef EFFECT_TILES_H
#define EFFECT_TILES_H

#include <stdint.h>

/* Forty-eight128-byte blocks: original16 plus32 in actor slots11/12
 * (tiles704..831). Dungeon actors/deaths are capped at11 slots.
 * Town plans use only original slots. Stairs880..883 stay intact.
 * A16px image occupies one block, a32px image four contiguous blocks. OAM
 * instances share the same immutable pixels. Frame plans assign disjoint
 * regions before submitting any OAM, with no affine enlargement. */
enum {
 EFFECT_TILE_FIRST=896,
 EFFECT_TILE_SLOTS=48,
 EFFECT_TILE_STRIDE=4,
 EFFECT_TILE_BYTES=128,
 EFFECT_TILE_NONE=1024
};
/* Extra dungeon images reuse the town-only reward tiles, preserving stairs. */
static inline unsigned effect_tile_address(unsigned i){return i>=16?704+(i-16)*4:i<3?868+i*4:884+(i-3)*4;}
typedef struct {
 const uint32_t *keys[EFFECT_TILE_SLOTS];
 uint16_t pinned;
} EffectTileCache;

/* effect_tiles.inc is included after dma() and UI_FN in the renderer.
 * invalidate(): after a VRAM ownership change.
 * place(p,slot,blocks): commit a prevalidated disjoint atlas plan; blocks1/4.
 * The plan caps large images at11 in dungeons (two in town) and overall blocks at48. Oversubscribed
 * images use their own direct-from-source16px form, never a different spell.
 * The legacy pinned field retains the existing static layout. */

#endif
