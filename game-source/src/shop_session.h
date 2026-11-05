#ifndef SHOP_SESSION_H
#define SHOP_SESSION_H
#include "game.h"
#include "shop.h"
enum { GAME_SHOP_SELL_SMITH=5,GAME_SHOP_SELL_WITCH=6,GAME_SHOP_REPAIR=7,
 GAME_SHOP_RECHARGE=8,GAME_SHOP_IDENTIFY=9,GAME_SHOP_WIRT_VIEW=10,GAME_SHOP_ACTIONS=11 };
/* Call with the acting hero selected; capture that hero after a commit.
 * Commands never depend on local menu state. Quote tokens bind the exact item
 * and price seen when entering confirmation, including premium row identity. */
unsigned game_shop_quote(Game *g,unsigned action,unsigned slot,Item *item,unsigned *price);
unsigned game_shop_check(Game *g,unsigned action,unsigned slot);
uint32_t game_shop_quote_token(Game *g,unsigned action,unsigned slot);
unsigned game_shop_command(Game *g,unsigned action,unsigned slot,uint32_t expected);
uint32_t game_shop_hash(uint32_t hash);
#endif
