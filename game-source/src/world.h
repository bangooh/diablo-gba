#pragma once
#include "game.h"
extern uint8_t game_cave_policy; /* 0: reference generation (offline tests), 2: bounded generator22 */
typedef struct { const uint8_t *art,*solid,*occludes; const uint16_t *macros,*tiles; unsigned macro_count,piece_count,height; const uint16_t *render_rows; /* Tiles: atlas index << 2 | opacity; optional compact piece rows. */ } WorldArt;
const WorldArt *game_world_art(unsigned level);
/* 0=caves,1=foul,2..33=native purification prefix steps0..31. */
unsigned game_water_palette_state(unsigned level);
void game_water_colors_reset(void);
int game_water_colors_step(const Game *g,unsigned previous_state);
void game_world_generate(Game *g,uint32_t seed);
void game_world_visuals(uint16_t *tiles,uint8_t *depth,uint32_t seed);
/* Expand a freshly prepared ordinary floor; caller verifies its lifetime. */
void game_world_visuals_prepared(uint16_t *tiles,uint8_t *depth);
void game_world_valor_change(Game *g,uint16_t *tiles,uint8_t *depth);
/* Install native L2/L3 doors in existing prop slots, preserving quest objects.
 * Returns occupied slots; an initially empty array is filled densely. */
unsigned game_world_doors(Game *g,uint32_t seed);
/* Immediately after game_world_generate, reuse its untouched native macro map. */
unsigned game_world_doors_prepared(Game *g);
/* Overlay immutable regenerated terrain with current archived door states. */
void game_world_doors_overlay(const Game *g,uint16_t *tiles,uint8_t *depth);
void game_world_door_overlay(const Game *g,unsigned index,uint16_t *tiles,uint8_t *depth);
/* Prepare unpublished native room IDs while the full generation arena is borrowed. */
void game_rooms_prepare_l1(const Game *g);
void game_rooms_prepare_fixed(unsigned level);
