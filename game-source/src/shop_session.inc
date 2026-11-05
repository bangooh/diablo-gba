/* Shared authoritative shop execution. Included after menu-owned town stock;
 * UI, selection and pending transport are deliberately absent from this file. */
#include "shop_session.h"
static UI_FN SmithStock *game_shop_stock(unsigned action){return action?&town_other[action-1]:&town_smith;}
static UI_FN uint32_t shop_hash_bytes(uint32_t hash,const void *data,unsigned bytes){
 const uint8_t *p=data;for(unsigned i=0;i<bytes;i++)hash=(hash^p[i])*16777619u;return hash;
}
uint32_t UI_FN game_shop_hash(uint32_t hash){
 hash=shop_hash_bytes(hash,&town_smith,sizeof town_smith);
 hash=shop_hash_bytes(hash,town_other,sizeof town_other);
 return shop_hash_bytes(hash,&town_premium,sizeof town_premium);
}
unsigned UI_FN game_shop_quote(Game *g,unsigned action,unsigned slot,Item *item,unsigned *price){
 if(!g||!g->area||g->hp<=0||g->gold<0||action>=GAME_SHOP_ACTIONS||slot>=64||!item||!price)return SHOP_INVALID;
 if(action==GAME_SHOP_WIRT_VIEW){
  if(slot)return SHOP_INVALID;
  unsigned unused;int result=vendor_quote(VENDOR_BOY,game_shop_stock(VENDOR_BOY),0,item,&unused);
  if(result==SHOP_OK)*price=50;return result;
 }
 if(action==VENDOR_PREMIUM)return premium_quote(game_shop_stock(action),&town_premium,slot,item,price);
 if(action<VENDOR_COUNT)return vendor_quote(action,game_shop_stock(action),slot,item,price);
 if(slot>=ITEM_SLOTS){
  if(action!=GAME_SHOP_SELL_WITCH||slot>=ITEM_SLOTS+10||!game_belt_valid(g->belt,g->potions))return SHOP_INVALID;
  unsigned kind=game_belt_kind(g,slot-ITEM_SLOTS);if(!kind)return SHOP_INVALID;*item=item_create(kind,1);
 }else *item=game_inventory()->items[slot];
 return shop_service_quote(action-GAME_SHOP_SELL_SMITH,item,price);
}
static UI_FN uint32_t shop_quote_hash(unsigned action,unsigned slot,const Item *item,unsigned price){
 const uint8_t header[]={(uint8_t)action,(uint8_t)slot,(uint8_t)price,(uint8_t)(price>>8),(uint8_t)(price>>16),(uint8_t)(price>>24)};
 uint32_t hash=shop_hash_bytes(2166136261u,header,sizeof header);hash=shop_hash_bytes(hash,item,sizeof *item);
 return hash?hash:1; /* Zero is reserved for an unavailable quote. */
}
uint32_t UI_FN game_shop_quote_token(Game *g,unsigned action,unsigned slot){
 Item item;unsigned price;if(game_shop_quote(g,action,slot,&item,&price)!=SHOP_OK)return 0;
 return shop_quote_hash(action,slot,&item,price);
}
static UI_FN unsigned shop_execute(Game *g,unsigned action,unsigned slot,uint32_t expected,int commit){
 Item item;unsigned price;unsigned result=game_shop_quote(g,action,slot,&item,&price);if(result!=SHOP_OK)return result;
 if(commit&&(!expected||expected!=shop_quote_hash(action,slot,&item,price)))return SHOP_INVALID;
 if(action==GAME_SHOP_WIRT_VIEW){if(price>(unsigned)g->gold)return SHOP_NO_GOLD;if(commit)g->gold-=price;return SHOP_OK;}
 if(action==GAME_SHOP_SELL_WITCH&&slot>=ITEM_SLOTS){
  if(price>(unsigned)GAME_GOLD_MAX-(unsigned)g->gold)return SHOP_GOLD_FULL;
  if(commit){game_belt_remove(g,slot-ITEM_SLOTS);g->gold+=price;}return SHOP_OK;
 }
 if(action>=VENDOR_COUNT){
  result=shop_service(action-GAME_SHOP_SELL_SMITH,slot,game_inventory(),&g->gold,commit);
  if(result==SHOP_OK&&commit)game_items_update(g);return result;
 }
 if(price>(unsigned)g->gold)return SHOP_NO_GOLD;
 if(game_belt_code(item.kind)&&!game_belt_valid(g->belt,g->potions))return SHOP_INVALID;
 if(game_belt_code(item.kind)&&g->potions<10){
  if(commit){
   if(!game_add_potion(g,item.kind))return SHOP_INVALID;g->gold-=price;
   if(!((action==VENDOR_WITCH&&slot<3)||(action==VENDOR_HEALER&&slot<2)))game_shop_stock(action)->sold|=1u<<slot;
  }return SHOP_OK;
 }
 if(action==VENDOR_PREMIUM)return premium_buy(game_shop_stock(action),&town_premium,slot,game_inventory(),&g->gold,commit);
 return vendor_buy(action,game_shop_stock(action),slot,game_inventory(),&g->gold,commit);
}
unsigned UI_FN game_shop_check(Game *g,unsigned action,unsigned slot){return shop_execute(g,action,slot,0,0);}
unsigned UI_FN game_shop_command(Game *g,unsigned action,unsigned slot,uint32_t expected){return shop_execute(g,action,slot,expected,1);}
