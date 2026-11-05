#pragma once
#include <stdint.h>
/* Retail Diablo difficulty; multiplayer is still selected by coop_active. */
enum { GAME_DIFFICULTY_NORMAL=0,GAME_DIFFICULTY_NIGHTMARE=1,
       GAME_DIFFICULTY_HELL=2,GAME_DIFFICULTY_COUNT=3 };
extern uint8_t game_difficulty;
/* Reject invalid values without changing the current campaign difficulty. */
int game_set_difficulty(unsigned difficulty);
/* Invalid display values use NORMAL; the setter still rejects them. */
const char *game_difficulty_name(unsigned difficulty);
unsigned game_monster_level(unsigned kind);
/* Native MData->mLevel: item affix/book/staff generation, never difficulty. */
unsigned game_monster_base_level(unsigned kind);
unsigned game_monster_accuracy(unsigned kind,int secondary);
unsigned game_monster_armor(unsigned kind);
uint32_t game_monster_xp(unsigned kind);
unsigned game_monster_damage_bound(unsigned kind,int secondary,int maximum);
unsigned game_monster_hp(unsigned kind,uint32_t roll);
/* Explicit SP/MP mode; any nonzero multiplayer value selects MP. Reads the
 * current campaign difficulty, never changes global mode or simulation state. */
unsigned game_monster_hp_bound_mode(unsigned kind,int maximum,unsigned multiplayer);
unsigned game_monster_hp_bound(unsigned kind,int maximum);
unsigned game_monster_immunities(unsigned kind);
unsigned game_monster_resistances(unsigned kind);

unsigned game_monster_hp_archive(unsigned kind,unsigned maximum);
unsigned game_monster_hp_unarchive(unsigned kind,unsigned code);
unsigned game_monster_history_index(unsigned kind);
