#pragma once
#include <stdint.h>
/* Caller-owned transaction scratch. Capture both the old and unpublished
 * candidate contexts; neither helper generates terrain or publishes rooms. */
typedef struct {
 uint8_t *room_candidate,*room_live;
 uint16_t quest_cells[9];
 int16_t butcher_room[2];
 uint8_t banner_open[100],valor_open[187],water_phase,cave_policy;
} GameGenerationState;
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(GameGenerationState)==320,"generation transaction context");
#endif
void game_generation_capture(GameGenerationState *out);
void game_generation_apply(const GameGenerationState *in);
/* Module helpers fill only their own fields in the shared snapshot. */
void game_world_generation_capture(GameGenerationState *out);
void game_world_generation_apply(const GameGenerationState *in);
void game_rooms_generation_capture(GameGenerationState *out);
void game_rooms_generation_apply(const GameGenerationState *in);
