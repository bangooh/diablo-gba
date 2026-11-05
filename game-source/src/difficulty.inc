/* Retail InitMonster / PlaceUniqueMonst / PlaceGroup stat order.
 * Included by game.c after its declarations. This replaces the previous HP
 * and resistance getter definitions when difficulty is integrated. Rendering
 * animation/body definitions remain independent of these source combat rows. */
#include "difficulty.h"
#include "../generated/monster_difficulty.inc"
#define DIFFICULTY_ROM __attribute__((section(".text.ui"),noinline))
uint8_t game_difficulty;
DIFFICULTY_ROM const char *game_difficulty_name(unsigned difficulty){
 return difficulty==GAME_DIFFICULTY_NIGHTMARE?"NIGHTMARE":difficulty==GAME_DIFFICULTY_HELL?"HELL":"NORMAL";
}
DIFFICULTY_ROM int game_set_difficulty(unsigned difficulty){
 if(difficulty>=GAME_DIFFICULTY_COUNT)return 0;
 game_difficulty=(uint8_t)difficulty;return 1;
}
static DIFFICULTY_ROM const MonsterDifficultyUnique *difficulty_unique(unsigned kind){
 unsigned unique=monster_difficulty_kinds[kind].unique;
 return unique&&!(unique&MONSTER_DIFFICULTY_PACK)?&monster_difficulty_uniques[unique-1]:0;
}
static DIFFICULTY_ROM unsigned difficulty_bonus(void){return game_difficulty==GAME_DIFFICULTY_NIGHTMARE?15:game_difficulty==GAME_DIFFICULTY_HELL?30:0;}
DIFFICULTY_ROM unsigned game_monster_base_level(unsigned kind){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 return monster_difficulty_bases[monster_difficulty_kinds[kind].base].level;
}
DIFFICULTY_ROM unsigned game_monster_level(unsigned kind){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);unsigned bonus=difficulty_bonus();
 /* An authored nonzero unique level replaces InitMonster's level. The zero
  * case instead adds5 to its already-adjusted level, then adjusts it again. */
 /* Retail MSVC char is signed. All current runtime levels remain<=95. */
 int level=(int8_t)(b->level+bonus);
 if(u)level=(int8_t)((u->level?(int8_t)(2*u->level):(int8_t)(level+5))+bonus);
 return(unsigned)level;
}
DIFFICULTY_ROM unsigned game_monster_accuracy(unsigned kind,int secondary){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 if(u&&(u->flags&4))return u->var1;
 return(uint8_t)((secondary?b->hit2:b->hit)+(game_difficulty==GAME_DIFFICULTY_NIGHTMARE?85:game_difficulty==GAME_DIFFICULTY_HELL?120:0));
}
DIFFICULTY_ROM unsigned game_monster_armor(unsigned kind){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 if(u&&(u->flags&8))return u->var1;
 return(uint8_t)(b->armor+(game_difficulty==GAME_DIFFICULTY_NIGHTMARE?50:game_difficulty==GAME_DIFFICULTY_HELL?80:0));
}
static DIFFICULTY_ROM uint32_t difficulty_xp(uint32_t xp){
 return(uint16_t)(game_difficulty==GAME_DIFFICULTY_NIGHTMARE?2*(xp+1000):game_difficulty==GAME_DIFFICULTY_HELL?4*(xp+1000):xp);
}
DIFFICULTY_ROM uint32_t game_monster_xp(unsigned kind){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 uint32_t xp=difficulty_xp(monster_difficulty_bases[monster_difficulty_kinds[kind].base].xp);
 /* Native uniques double InitMonster's adjusted XP and then apply the
  * PlaceUniqueMonst difficulty adjustment a second time. Each assignment
  * writes unsigned-short mExp, so retain narrowing at both boundaries. */
 return difficulty_unique(kind)?difficulty_xp((uint16_t)(2*xp)):xp;
}
DIFFICULTY_ROM unsigned game_monster_damage_bound(unsigned kind,int secondary,int maximum){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 unsigned value=u?(maximum?u->damage_max:u->damage_min):secondary?(maximum?b->damage2_max:b->damage2_min):(maximum?b->damage_max:b->damage_min);
 /* PlaceUniqueMonst replaces both attacks before difficulty scaling. */
 return(uint8_t)(game_difficulty==GAME_DIFFICULTY_NIGHTMARE?2*(value+2):game_difficulty==GAME_DIFFICULTY_HELL?4*value+6:value);
}
static DIFFICULTY_ROM unsigned difficulty_hp(unsigned kind,unsigned raw,unsigned multiplayer){
 unsigned q6=raw<<6;
 if(!multiplayer){q6>>=1;if(q6<64)q6=64;}
 if(game_difficulty==GAME_DIFFICULTY_NIGHTMARE)q6=3*q6+64;
 else if(game_difficulty==GAME_DIFFICULTY_HELL)q6=4*q6+192;
 if(monster_difficulty_kinds[kind].unique&MONSTER_DIFFICULTY_PACK)q6*=2;
 /* Keep all native half-HP fractions through difficulty and PlaceGroup.
  * The integer-HP runtime rounds once, at this final representation boundary. */
 return(q6+63)>>6;
}
DIFFICULTY_ROM unsigned game_monster_hp(unsigned kind,uint32_t roll){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 return difficulty_hp(kind,u?u->hp:b->hp_min+roll%(b->hp_max-b->hp_min+1),coop_active);
}
DIFFICULTY_ROM unsigned game_monster_hp_bound_mode(unsigned kind,int maximum,unsigned multiplayer){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 return difficulty_hp(kind,u?u->hp:maximum?b->hp_max:b->hp_min,multiplayer);
}
DIFFICULTY_ROM unsigned game_monster_hp_bound(unsigned kind,int maximum){
 return game_monster_hp_bound_mode(kind,maximum,coop_active);
}
static DIFFICULTY_ROM unsigned difficulty_resists(unsigned kind){
 if(kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);
 return u?u->resists:game_difficulty==GAME_DIFFICULTY_HELL?b->hell_resists:b->resists;
}
DIFFICULTY_ROM unsigned game_monster_immunities(unsigned kind){return difficulty_resists(kind)&7;}
DIFFICULTY_ROM unsigned game_monster_resistances(unsigned kind){return(difficulty_resists(kind)>>3)&7;}
#undef DIFFICULTY_ROM
/* Archive a native HP roll in seven bits, plus its original player mode.
 * All retail normal HP ranges have <=106 outcomes; unique HP is fixed. */
__attribute__((section(".text.ui"),noinline)) unsigned game_monster_hp_unarchive(unsigned kind,unsigned code){
 if(code==255||kind>=MONSTER_DIFFICULTY_TYPES)return 0;
 const MonsterDifficultyBase *b=&monster_difficulty_bases[monster_difficulty_kinds[kind].base];
 const MonsterDifficultyUnique *u=difficulty_unique(kind);unsigned roll=code&127;
 if(roll>(u?0:b->hp_max-b->hp_min))return 0;
 return difficulty_hp(kind,u?u->hp:b->hp_min+roll,code>>7);
}
__attribute__((section(".text.ui"),noinline)) unsigned game_monster_hp_archive(unsigned kind,unsigned maximum){
 if(!maximum)return 255;
 for(unsigned mode=0;mode<2;mode++)for(unsigned roll=0;roll<127;roll++){
  unsigned value=game_monster_hp_unarchive(kind,roll|(mode<<7));
  if(value==maximum)return roll|(mode<<7);if(!value)break;
 }
 return 256;
}

__attribute__((section(".text.ui"),noinline)) unsigned game_monster_history_index(unsigned kind){return kind<MONSTER_DIFFICULTY_TYPES?monster_difficulty_kinds[kind].base:0;}
