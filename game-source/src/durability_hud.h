#ifndef DURABILITY_HUD_H
#define DURABILITY_HUD_H
#include "items.h"

/* Retail control.cpp DrawDurIcon: head, chest, left hand, right hand;
 * <=5 durability, yellow at 3..5 and red at <=2. Use decoded durability:
 * staff charges and the non-staff high durability byte are different fields. */
#ifndef DURABILITY_FN
#define DURABILITY_FN static inline
#endif
enum { DURABILITY_WARNINGS=4, DURABILITY_YELLOW=16 };
DURABILITY_FN unsigned durability_hud_icon(const Item *item){
 if(item->kind>=ITEM_COUNT||!item_defs[item->kind].durability)return 0;
 unsigned icon,slot=item->pos-BAG_CELLS;
 if(slot==EQUIP_HEAD)icon=4;
 else if(slot==EQUIP_CHEST)icon=3;
 else if(slot==EQUIP_HAND||slot==EQUIP_SHIELD){
  const BaseItemDef *base=item_base(item->kind);if(!base)return 0;
  switch(base->type){
   case 1:icon=2;break;case 2:icon=6;break;case 3:icon=7;break;
   case 4:icon=5;break;case 5:icon=1;break;case 10:icon=8;break;
   default:return 0;
  }
 }else return 0;
 unsigned durability=item_durability(item);
 if(durability>5)return 0; /* Also excludes ITEM_INDESTRUCTIBLE. */
 return icon|(durability>2?DURABILITY_YELLOW:0);
}
DURABILITY_FN unsigned durability_hud_collect(const Inventory *bag,uint8_t icons[DURABILITY_WARNINGS]){
 uint8_t slots[DURABILITY_WARNINGS]={0};
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *item=&bag->items[i];unsigned n;
  switch(item->pos){
   case BAG_CELLS+EQUIP_HEAD:n=0;break;
   case BAG_CELLS+EQUIP_CHEST:n=1;break;
   case BAG_CELLS+EQUIP_HAND:n=2;break;
   case BAG_CELLS+EQUIP_SHIELD:n=3;break;
   default:continue;
  }
  unsigned icon=durability_hud_icon(item);if(icon)slots[n]=(uint8_t)icon;
 }
 unsigned count=0;
 for(unsigned n=0;n<DURABILITY_WARNINGS;n++)if(slots[n])icons[count++]=slots[n];
 for(unsigned n=count;n<DURABILITY_WARNINGS;n++)icons[n]=0;
 return count;
}
#endif
