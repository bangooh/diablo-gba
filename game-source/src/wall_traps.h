#pragma once
#include "game.h"
/* Called after every ordinary/quest prop has its final stable slot, while
 * the complete generator arena (and L2 macro map at its start) is available. */
void game_wall_traps_generate(Game *g,uint32_t floor_seed);
void game_wall_traps_step(Game *g);
int game_wall_trap_linked(const Game *g,unsigned target);
int game_wall_trap_armed(const Game *g,unsigned target);
unsigned game_wall_trap_frame(unsigned kind);
/* Dungeon's authored set-piece exclusion adapter. */
int game_wall_trap_protected(int x,int y);
