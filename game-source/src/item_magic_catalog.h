#pragma once
#include <stdint.h>
typedef struct { const char *name; int32_t types; int16_t power,param1,param2; int32_t min_value,max_value; int16_t multiplier; uint8_t min_level,alignment,double_weight,good; } ItemAffix;
typedef struct { int16_t power,low,high; } ItemPower;
typedef struct { const char *name; int32_t value; uint8_t base,min_level,count,reserved; ItemPower powers[6]; } UniqueItemDef;
#define ITEM_PREFIX_COUNT 83
#define ITEM_SUFFIX_COUNT 95
#define ITEM_UNIQUE_COUNT 90
extern const ItemAffix item_prefixes[ITEM_PREFIX_COUNT],item_suffixes[ITEM_SUFFIX_COUNT];
extern const UniqueItemDef item_uniques[ITEM_UNIQUE_COUNT];
