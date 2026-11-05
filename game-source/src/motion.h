#pragma once
#include <stdint.h>
/* Coordinates remain signed Q8. Animation durations are at most72 frames,
 * and directions0..7; byte clocks save8 bytes for each of117 live actors. */
typedef struct { int32_t x,y,sx,sy,tx,ty; uint8_t elapsed,total,dir; } Motion;
_Static_assert(sizeof(Motion)==28,"compact motion storage");
