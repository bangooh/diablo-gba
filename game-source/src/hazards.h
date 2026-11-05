#ifndef HAZARDS_H
#define HAZARDS_H
#include "game.h"
enum { GAME_TRAP_ARROW, GAME_TRAP_FARROW, GAME_TRAP_NOVA, GAME_TRAP_FIREBOLT, GAME_TRAP_LIGHTNING, GAME_TRAP_COUNT };
/* Low four bits retain direction16. Player spell sidecars use bit7. */
enum { HOSTILE_FARROW=0x10, HOSTILE_BURST=0x20, HOSTILE_PROJECTILE=0x40, HOSTILE_TRAP_OWNER=255 };
/* Bounded pool; no eviction. Nova emits at most eight rays. */
int game_trap_fire(Game *g,int sx,int sy,int tx,int ty,unsigned type,uint32_t seed);
/* Native environmental contact: range rolls and per-target hit/block checks.
 * LIGHTNING ranges are Q6, as in retail MI_Lightctrl/MI_Lightning.
 * FARROW means the elemental stationary phase; ARROW is its physical flight.
 * Returns successful contacts, including blocked hits. Caller owns rng. */
int game_trap_hit_cell(Game *g,int x,int y,unsigned min_mon,unsigned max_mon,unsigned min_hero,unsigned max_hero,unsigned dist,unsigned type,uint32_t *rng);
#endif
