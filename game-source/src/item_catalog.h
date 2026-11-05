#ifndef ITEM_CATALOG_H
#define ITEM_CATALOG_H
#include <stdint.h>
#define BASE_ITEM_COUNT 156
/* Native Diablo IDs, independent of stable prototype inventory IDs. */
typedef struct {
 const char *name,*short_name;
 uint32_t effects;
 uint16_t value,max_value,cursor;
 uint8_t drop_rate,item_class;
 int8_t location,type;
 uint8_t unique_base,min_level,durability,min_damage,max_damage,min_ac,max_ac;
 uint8_t strength,magic,dexterity,misc,spell,usable;
} BaseItemDef;
extern const BaseItemDef base_item_defs[BASE_ITEM_COUNT];
const BaseItemDef *item_base(unsigned kind);
#endif
