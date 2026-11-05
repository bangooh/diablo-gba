#pragma once
#include <stdint.h>
void world_generate(unsigned level,uint32_t seed,uint8_t *macros,int *up,int *down);
void world_generate_quest(unsigned level,uint32_t seed,uint8_t *macros,int *up,int *down,int anvil,int *cell);
/* Native floor5 Valor terrain foundation. stones=0..3, clamped at3.
 * origin is the macro coordinate, before 2x cell expansion. The campaign opts in for the selected single-player Valor quest. */
void world_generate_valor(uint32_t seed,unsigned stones,uint8_t *macros,int *up,int *down,int *origin);
void world_valor_open_tiles(uint8_t *out);
void world_generate_blind(uint32_t seed,unsigned opened,uint8_t *macros,int *up,int *down,int *origin);
void world_blind_open_tiles(uint8_t *out);

void world_diablo_layout(unsigned gates,uint8_t *out,uint16_t *origins);

void world_generate_late(unsigned level,uint32_t seed,unsigned opened,uint8_t *out,int *up,int *down,int *origin);
void world_late_open_tiles(uint8_t *out,unsigned width,unsigned height);
