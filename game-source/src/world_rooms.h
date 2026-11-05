#pragma once
#include <stdint.h>

/* Generator arena offsets. Only the cropped map survives during play.
 * The renderer reserves LIVE_BYTES by reducing its prop art cache to six slots. */
enum {
 GAME_ROOMS_NATIVE_SIDE=112, GAME_ROOMS_SIDE=80,
 GAME_ROOMS_CANDIDATE_OFFSET=82000, GAME_ROOMS_NATIVE_BYTES=112*112,
 GAME_ROOMS_LIVE_OFFSET=150976, GAME_ROOMS_LIVE_BYTES=80*80,
 GAME_ROOMS_STACK_OFFSET=82000+112*112,
 GAME_ROOMS_SCRATCH_END=82000+112*112+40*40*2
};

/* begin captures dungeon_workspace. Changing that pointer afterwards is safe.
 * Generating a candidate never changes the live map or its validity. */
void game_rooms_begin(void);
void game_rooms_invalidate(void);
uint8_t *game_rooms_candidate(void);
int game_room_candidate_at(int x,int y);
int game_room_at(int x,int y);
/* Publish crops [16,96) from the captured arena, then retires the candidate.
 * An invalid candidate invalidates live IDs; call exactly once after commit.
 * It must precede any renderer reuse of the generation scratch. */
void game_rooms_publish(void);

/* Inclusive native 112-grid coordinates. Explicit IDs preserve the allocation
 * counter; -1 allocates the next native ID (starting at 1). ID 0 is valid.
 * Unsupported/outside coordinates are clipped; inverted rectangles do nothing. */
void game_rooms_rect(int x1,int y1,int x2,int y2,int id);
void game_rooms_macro_rect(int x1,int y1,int x2,int y2,int id);
void game_rooms_live_rect(int x1,int y1,int x2,int y2,int id);

/* Native L2/L4/L5 FTVR visitation order, without recursive GBA stack usage.
 * Macros are 40x40; stride describes row or column pitch. Outside macros are
 * non-floor, but their native micro-border receives the original edge stamps.
 * Exhausting the nonzero native ID namespace invalidates the candidate. */
void game_rooms_flood(const uint8_t *macros,unsigned stride,int x_major,unsigned floor);
