#pragma once
#include <stdint.h>
extern uint8_t game_cave_policy;
uint32_t world_fast_seed(unsigned level,unsigned variant,unsigned index);
#ifdef GEN_PROFILE
extern unsigned gen_attempts;
extern uint32_t gen_candidate_seed;
#define GEN_RECORD(s) (++gen_attempts,gen_candidate_seed=(s))
#else
#define GEN_RECORD(s) ((void)0)
#endif
/* Deterministic work accounting keeps save/load and peers independent of
 * host speed. Budgets reserve time for fallback generation and scene setup. */
extern uint32_t world_generation_work;
#define GEN_WORK() (++world_generation_work)
static inline void world_seed_candidate(uint32_t *s,unsigned level,unsigned variant,uint32_t initial,unsigned *attempts){
 static const uint32_t budget[4]={12000,1000,24000,12000};
 if(!*attempts)world_generation_work=0;
 ++*attempts;
 if(game_cave_policy>=2&&world_generation_work>=budget[(level-1)/4])
  *s=world_fast_seed(level,variant,((initial^(initial>>16))+*attempts-1)%120);
 world_generation_work+=1000;
 GEN_RECORD(*s);
}
