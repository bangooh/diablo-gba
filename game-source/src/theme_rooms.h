#pragma once
#include <stdint.h>
/* Generation-only descriptors live inside the existing WorldState arena. */
enum { WORLD_THEME_LIMIT=50 };
typedef struct { uint8_t x,y,width,height,room; } WorldThemeRoom;
unsigned game_world_theme_count(void);
int game_world_theme_get(unsigned index,WorldThemeRoom *out);
