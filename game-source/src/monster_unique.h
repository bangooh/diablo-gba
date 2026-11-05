#ifndef MONSTER_UNIQUE_H
#define MONSTER_UNIQUE_H
/* Included by game.h after Game is defined. Normal image bodies remain0..49. */
enum { MONSTER_UNIQUE_FIRST=50,MONSTER_UNIQUE_COUNT=72,
       MONSTER_PACK_MINION_FIRST=54 };
unsigned game_monster_base_kind(unsigned kind);
unsigned game_monster_corpse_kind(unsigned kind);
int game_monster_is_unique(unsigned kind);
int game_monster_is_pack_minion(unsigned kind);
unsigned game_unique_palette_family(unsigned kind);
unsigned game_unique_minion_kind(unsigned leader_kind);
unsigned game_unique_leader_kind(unsigned member_kind);
int game_unique_base_on_floor(unsigned base,unsigned floor);
unsigned game_unique_for_roster(unsigned floor,const uint8_t *roster,unsigned count);
void game_unique_restore(const Game *g);
#endif
