#ifndef ITEM_EFFECT_CACHE_H
#define ITEM_EFFECT_CACHE_H
#include "game.h"
typedef struct {Item equipped[EQUIP_COUNT];uint16_t hp,mana;uint8_t stats[4];unsigned cls;} ItemEffectKey;
typedef struct {ItemEffectKey key;ItemEffects value;} ItemEffectCache;
ItemEffectCache *audio_item_cache(void);
void item_effect_cache_reset(void);
void item_effect_cache_step(unsigned begin);
#endif
