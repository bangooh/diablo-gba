#ifndef SHOP_H
#define SHOP_H
#include "items.h"
enum { SMITH_STOCK_MAX=19 };
/* Caller owns lifecycle/persistence. Slot IDs remain stable after purchases. */
typedef struct { uint32_t seed,sold; uint8_t level,reserved[3]; } SmithStock;
typedef struct { uint32_t seed[6];uint8_t level[6];uint16_t reserved; } PremiumRows;
int premium_valid(const SmithStock *stock,const PremiumRows *rows);
int premium_refresh(SmithStock *stock,PremiumRows *rows,unsigned level,int fresh);
int premium_quote(const SmithStock *stock,const PremiumRows *rows,unsigned slot,Item *item,unsigned *price);
int premium_buy(SmithStock *stock,PremiumRows *rows,unsigned slot,Inventory *bag,GameGold *gold,int commit);
enum { SHOP_OK=1,SHOP_INVALID,SHOP_SOLD,SHOP_NO_GOLD,SHOP_NO_ROOM,SHOP_GOLD_FULL };
enum { SHOP_SMITH_SELL,SHOP_WITCH_SELL,SHOP_REPAIR,SHOP_RECHARGE,SHOP_IDENTIFY };
enum { VENDOR_SMITH,VENDOR_PREMIUM,VENDOR_WITCH,VENDOR_HEALER,VENDOR_BOY,VENDOR_COUNT };
unsigned vendor_count(unsigned vendor,const SmithStock *stock);
int vendor_valid(unsigned vendor,const SmithStock *stock);
int vendor_quote(unsigned vendor,const SmithStock *stock,unsigned slot,Item *item,unsigned *price);
int vendor_buy(unsigned vendor,SmithStock *stock,unsigned slot,Inventory *bag,GameGold *gold,int commit);
unsigned shop_item_value(const Item *item,int identified);
int shop_service_quote(unsigned service,const Item *item,unsigned *price);
int shop_service(unsigned service,unsigned slot,Inventory *bag,GameGold *gold,int commit);
unsigned smith_store_level(unsigned deepest_visited,unsigned hero_level,int multiplayer);
int smith_stock_init(SmithStock *stock,uint32_t seed,unsigned level);
int smith_stock_valid(const SmithStock *stock);
unsigned smith_stock_count(const SmithStock *stock);
/* Read-only quote; failure leaves outputs untouched. Price is never client input. */
unsigned smith_stock_list(const SmithStock *s,Item stock[SMITH_STOCK_MAX]);
int smith_stock_quote(const SmithStock *stock,unsigned slot,Item *item,unsigned *price);
int smith_stock_check(const SmithStock *stock,unsigned slot,const Inventory *bag,GameGold gold);
/* Atomic bag/gold/sold update, independent of UI and current co-op owner globals. */
int smith_stock_buy(SmithStock *stock,unsigned slot,Inventory *bag,GameGold *gold);
#endif
