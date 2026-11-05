#include "shop.h"
#include "game.h"
#ifndef SHOP_FN
#define SHOP_FN __attribute__((section(".text.ui")))
#endif
_Static_assert(sizeof(SmithStock)==12,"compact reconstructible smith stock");
/* Retail items.cpp: SmithItemOk/RndSmithItem/SpawnSmith/SortSmith.
 * Preserve pool weights and native base ordering, using the port's hash RNG.
 * stores.cpp S_SBuyEnter checks gold then bag space, not equip requirements.
 */
static SHOP_FN int eligible(unsigned kind,unsigned level){
 const BaseItemDef *b=item_base(kind);
 return b&&b->drop_rate&&b->min_level<=level&&b->type>=1&&b->type<=9&&b->value;
}
static SHOP_FN unsigned count(uint32_t seed){return 10+item_seed(seed^0x534d4954u)%10;}
SHOP_FN unsigned smith_store_level(unsigned deepest,unsigned hero_level,int multiplayer){
 unsigned level=multiplayer?hero_level/2:deepest;
 return level<4?6:level>14?16:level+2;
}
SHOP_FN int smith_stock_valid(const SmithStock *s){
 return s&&s->level>=1&&s->level<=50&&!s->reserved[0]&&!s->reserved[1]&&!s->reserved[2]&&!(s->sold>>count(s->seed));
}
SHOP_FN int smith_stock_init(SmithStock *s,uint32_t seed,unsigned level){
 if(!s||level<1||level>50)return 0;
 *s=(SmithStock){.seed=seed,.level=(uint8_t)level};return 1;
}
SHOP_FN unsigned smith_stock_count(const SmithStock *s){return smith_stock_valid(s)?count(s->seed):0;}
/* Reconstruct all sorted rows once for a read-only UI snapshot. */
SHOP_FN unsigned smith_stock_list(const SmithStock *s,Item stock[SMITH_STOCK_MAX]){
 if(!stock||!smith_stock_valid(s))return 0;
 uint8_t pool[ITEM_COUNT*2];unsigned total=0;
 /* Native catalog order, independent of the port's stable runtime IDs. */
 for(unsigned base=1;base<BASE_ITEM_COUNT;base++)for(unsigned k=1;k<ITEM_COUNT;k++){
  if(item_base(k)!=&base_item_defs[base]||!eligible(k,s->level))continue;
  pool[total++]=(uint8_t)k;
  if(item_base(k)->drop_rate==2)pool[total++]=(uint8_t)k;
 }
 if(!total)return 0;
 unsigned n=count(s->seed);
 for(unsigned i=0;i<n;i++){
  uint32_t seed=item_seed(s->seed+0x9e3779b9u*(i+1));unsigned kind=pool[item_seed(seed^0x425559u)%total];
  Item it=item_create_level(kind,seed,s->level);item_set_durability(&it,item_max_durability(&it));
  unsigned j=i;
  while(j&&item_base(stock[j-1].kind)>item_base(it.kind)){stock[j]=stock[j-1];j--;}
  stock[j]=it;
 }
 return n;
}
SHOP_FN int smith_stock_quote(const SmithStock *s,unsigned slot,Item *out,unsigned *price){
 if(!out||!price||!smith_stock_valid(s)||slot>=count(s->seed))return SHOP_INVALID;
 if(s->sold&(1u<<slot))return SHOP_SOLD;
 Item stock[SMITH_STOCK_MAX];if(!smith_stock_list(s,stock))return SHOP_INVALID;
 *out=stock[slot];*price=item_base(out->kind)->value;return SHOP_OK;
}
static SHOP_FN int prepare(const SmithStock *s,unsigned slot,const Inventory *bag,GameGold gold,Inventory *staged,unsigned *price){
 if(!bag||gold<0||!items_valid(bag))return SHOP_INVALID;
 Item item;int result=smith_stock_quote(s,slot,&item,price);
 if(result!=SHOP_OK)return result;
 if((unsigned)gold<*price)return SHOP_NO_GOLD;
 *staged=*bag;
 if(!items_add(staged,item))return SHOP_NO_ROOM;
 return SHOP_OK;
}
SHOP_FN int smith_stock_check(const SmithStock *s,unsigned slot,const Inventory *bag,GameGold gold){
 Inventory staged;unsigned price;return prepare(s,slot,bag,gold,&staged,&price);
}
SHOP_FN int smith_stock_buy(SmithStock *s,unsigned slot,Inventory *bag,GameGold *gold){
 if(!gold)return SHOP_INVALID;
 Inventory staged;unsigned price;int result=prepare(s,slot,bag,*gold,&staged,&price);
 if(result!=SHOP_OK)return result;
 *bag=staged;*gold=(GameGold)(*gold-price);s->sold|=1u<<slot;return SHOP_OK;
}
#include "../generated/shop_spell_costs.h"
/* Base value is immutable: spending staff charges must not lower sale price. */
SHOP_FN unsigned shop_item_value(const Item *it,int identified){
 const BaseItemDef *b=item_base(it->kind);if(!b||!item_valid(it))return 0;
 int base=b->value;unsigned spell=item_spell(it);
 if(item_is_spell(it)){
  if(item_scroll(it)){
   for(unsigned i=0;i<BASE_ITEM_COUNT;i++)if((base_item_defs[i].misc==21||base_item_defs[i].misc==22)&&base_item_defs[i].spell==spell)return base_item_defs[i].value;
   return 0;
  }
  if(spell<37)base+=shop_spell_cost[spell][0];
 }
 const UniqueItemDef *u=item_unique(it);
 if(item_is_staff(it)&&spell&&spell<37&&!u){
  unsigned initial=spell_defs[spell].staff_min+item_seed(it->seed^0x119u)%(spell_defs[spell].staff_max-spell_defs[spell].staff_min+1);
  if(it->flags&128)base=520;else base+=initial*shop_spell_cost[spell][1]/5;
 }
 if(!identified||!it->enchant)return base;
 if(u)return u->value;
 int add=0,mult=0;
 for(unsigned i=0;i<2;i++){
  const ItemAffix *a=item_affix(it,i);if(!a)continue;
  int roll=a->param1;
  if(a->param2>=a->param1)roll+=item_seed(it->seed^(i?0x5aff1u:0x9aff1u))%(a->param2-a->param1+1);
  int value=a->min_value;
  if(a->param1!=a->param2&&a->min_value!=a->max_value)value+=(a->max_value-a->min_value)*(100*(roll-a->param1)/(a->param2-a->param1))/100;
  add+=value;mult+=a->multiplier;
 }
 int value=add+(mult>0?base*mult:mult<0?base/mult:0);
 return value>0?(unsigned)value:1;
}
SHOP_FN int shop_service_quote(unsigned service,const Item *it,unsigned *price){
 if(!it||!price||!item_valid(it))return SHOP_INVALID;
 const BaseItemDef *b=item_base(it->kind);if(!b)return SHOP_INVALID;
 unsigned value=shop_item_value(it,!item_unidentified(it)),cost;
 if(service==SHOP_SMITH_SELL||service==SHOP_WITCH_SELL){
  if(it->pos>=BAG_CELLS||b->item_class==5||(it->kind==ITEM_STAFF_OF_LAZARUS||it->kind==ITEM_SPECTRAL_ELIXIR))return SHOP_INVALID;
  int witch=b->type==0||b->type==10;
  if(b->type==11||b->type<0||(service==SHOP_WITCH_SELL)!=witch)return SHOP_INVALID;
  cost=value/4;if(!cost)cost=1;
 }else if(service==SHOP_IDENTIFY){
  if(!item_unidentified(it))return SHOP_INVALID;cost=100;
 }else if(service==SHOP_REPAIR){
  unsigned max=item_max_durability(it),now=item_durability(it);
  if(!max||max==ITEM_INDESTRUCTIBLE||now>=max||b->type==0||b->type==11)return SHOP_INVALID;
  int magic=it->enchant&&!item_unidentified(it);
  if(magic)value=30*value/100;
  cost=value*(100*(max-now)/max)/100;
  if(!cost){if(magic)return SHOP_INVALID;cost=1;}if(cost>1)cost>>=1;
 }else if(service==SHOP_RECHARGE){
  unsigned spell=item_spell(it);
  if(!item_is_staff(it)||!spell||spell>=37||!it->max_charges||it->charges>=it->max_charges)return SHOP_INVALID;
  value=shop_item_value(it,0)+shop_spell_cost[spell][1];
  cost=(value*(100*(it->max_charges-it->charges)/it->max_charges)/100)>>1;
 }else return SHOP_INVALID;
 *price=cost;return SHOP_OK;
}
SHOP_FN int shop_service(unsigned service,unsigned slot,Inventory *bag,GameGold *gold,int commit){
 if(!bag||!gold||*gold<0||slot>=ITEM_SLOTS||!items_valid(bag))return SHOP_INVALID;
 Item *it=&bag->items[slot];unsigned price;int result=shop_service_quote(service,it,&price);if(result!=SHOP_OK)return result;
 int selling=service==SHOP_SMITH_SELL||service==SHOP_WITCH_SELL;
 if(selling){if(price>(unsigned)GAME_GOLD_MAX-(unsigned)*gold)return SHOP_GOLD_FULL;}
 else if(price>(unsigned)*gold)return SHOP_NO_GOLD;
 if(!commit)return SHOP_OK;
 if(selling){*it=(Item){.kind=ITEM_EMPTY};*gold+=price;}
 else{
  if(service==SHOP_REPAIR)item_set_durability(it,item_max_durability(it));
  else if(service==SHOP_RECHARGE)it->charges=it->max_charges;
  else it->enchant|=0x8000;
  *gold-=price;
 }
 return SHOP_OK;
}
SHOP_FN unsigned vendor_count(unsigned v,const SmithStock *s){
 if(!s||v>=VENDOR_COUNT||s->level<1||s->level>50)return 0;
 return v==VENDOR_SMITH?count(s->seed):v==VENDOR_PREMIUM?6:v==VENDOR_BOY?1:10+item_seed(s->seed^0x76656e64u)%8;
}
static SHOP_FN unsigned renewable(unsigned v){return v==VENDOR_WITCH?3:v==VENDOR_HEALER?2:0;}
SHOP_FN int vendor_valid(unsigned v,const SmithStock *s){
 unsigned n=vendor_count(v,s);return n&&!s->reserved[0]&&!s->reserved[1]&&!s->reserved[2]&&!(s->sold>>n)&&!(s->sold&((1u<<renewable(v))-1));
}
static SHOP_FN unsigned mapped_kind(unsigned id){
 const BaseItemDef *b=&base_item_defs[id];
 if(b->misc==24)return ITEM_BOOK_FIRE;
 if((b->misc==21||b->misc==22)&&game_spell_supported(b->spell))return ITEM_BOOK_FIRE;
 for(unsigned k=1;k<ITEM_COUNT;k++)if(item_base(k)==b)return k;
 return ITEM_EMPTY;
}
static SHOP_FN int vendor_candidate(unsigned v,unsigned id,unsigned low,unsigned high){
 const BaseItemDef *b=&base_item_defs[id];
 if(!b->drop_rate||b->min_level<low||b->min_level>high||mapped_kind(id)==ITEM_EMPTY)return 0;
 if(v==VENDOR_PREMIUM||v==VENDOR_BOY)return b->type>=1&&b->type<=13&&b->type!=10&&b->type!=11;
 if(v==VENDOR_HEALER)return b->type==0&&((b->misc==21&&b->spell==SPELL_HEAL)||(b->misc>=10&&b->misc<=13)||b->misc==18||b->misc==19);
 return (b->type==0||b->type==10)&&b->misc!=2&&b->misc!=3&&b->misc!=6&&b->misc!=7&&b->spell!=SPELL_TOWN&&b->spell!=SPELL_RESURRECT&&b->spell!=SPELL_HEAL_OTHER;
}
static SHOP_FN Item vendor_item(unsigned id,uint32_t seed,unsigned level){
 unsigned kind=mapped_kind(id);Item it=item_create_level(kind,seed,level);const BaseItemDef *b=&base_item_defs[id];
 if(b->misc==24)it.flags=game_spell_book(item_seed(seed),level?level:1);
 else if((b->misc==21||b->misc==22)&&game_spell_supported(b->spell))it.flags=128|b->spell;
 item_set_durability(&it,item_max_durability(&it));return it;
}
SHOP_FN int vendor_quote(unsigned v,const SmithStock *s,unsigned slot,Item *out,unsigned *price){
 if(v==VENDOR_SMITH)return smith_stock_quote(s,slot,out,price);
 if(!out||!price||!vendor_valid(v,s)||slot>=vendor_count(v,s))return SHOP_INVALID;
 if(s->sold&(1u<<slot))return SHOP_SOLD;
 if(slot<renewable(v)){
  unsigned kind=v==VENDOR_HEALER?(slot?ITEM_FULL_HEAL:ITEM_HEAL):slot==0?ITEM_MANA:slot==1?ITEM_FULL_MANA:ITEM_BOOK_FIRE;
  Item it=item_create_level(kind,item_seed(s->seed+slot),1);
  if(v==VENDOR_WITCH&&slot==2)it.flags=128|SPELL_TOWN;
  *out=it;*price=shop_item_value(&it,1);return SHOP_OK;
 }
 unsigned start=renewable(v),n=vendor_count(v,s);Item stock[19];uint8_t ids[19];
 static const int8_t offsets[6]={-1,-1,0,0,1,2};
 uint8_t pool[BASE_ITEM_COUNT];unsigned total=0,old_low=~0u,old_high=~0u;
 for(unsigned i=start;i<n;i++){
  int level=s->level;if(v==VENDOR_PREMIUM){level+=offsets[i];if(level<1)level=1;if(level>30)level=30;}
  unsigned low=v==VENDOR_PREMIUM?(unsigned)level/4:0;
  if(low!=old_low||(unsigned)level!=old_high){total=0;for(unsigned id=1;id<BASE_ITEM_COUNT;id++)if(vendor_candidate(v,id,low,level))pool[total++]=id;old_low=low;old_high=level;}
  if(!total)return SHOP_INVALID;
  Item it={0};unsigned id=0,ok=0;
  for(unsigned attempt=0;attempt<1024;attempt++){
   uint32_t seed=item_seed(s->seed+(i+1)*0x9e3779b9u+attempt*0x45d9f3bu);id=pool[item_seed(seed)%total];it=vendor_item(id,seed,level);
   if(v==VENDOR_PREMIUM)item_generate_store_magic(&it,level/2,level);
   else if(v==VENDOR_BOY)item_generate_store_magic(&it,level,2*level);
   else if(v==VENDOR_WITCH&&(item_is_staff(&it)||item_seed(seed^51)%100<=5))item_generate_store_magic(&it,level,2*level);
   if(item_valid(&it)&&shop_item_value(&it,1)<=(v==VENDOR_BOY?90000u:140000u)){ok=1;break;}
  }
  if(!ok)return SHOP_INVALID;
  unsigned j=i;if(v==VENDOR_WITCH||v==VENDOR_HEALER)while(j>start&&ids[j-1]>id){stock[j]=stock[j-1];ids[j]=ids[j-1];j--;}
  stock[j]=it;ids[j]=id;
 }
 *out=stock[slot];*price=shop_item_value(out,1);if(v==VENDOR_BOY)*price+=*price/2;return SHOP_OK;
}
SHOP_FN int vendor_buy(unsigned v,SmithStock *s,unsigned slot,Inventory *bag,GameGold *gold,int commit){
 if(!bag||!gold||*gold<0||!items_valid(bag))return SHOP_INVALID;
 Item it;unsigned price;int result=vendor_quote(v,s,slot,&it,&price);if(result!=SHOP_OK)return result;
 if(price>(unsigned)*gold)return SHOP_NO_GOLD;
 Inventory staged=*bag;if(!items_add(&staged,it))return SHOP_NO_ROOM;
 if(commit){*bag=staged;*gold-=price;if(slot>=renewable(v))s->sold|=1u<<slot;}
 return SHOP_OK;
}
/* Retail SpawnPremium retains four rows on level-up and refills a purchase
 * immediately. Persist row seeds/creation levels rather than six full items. */
static SHOP_FN void premium_replace(SmithStock *s,PremiumRows *rows,unsigned slot){
 static const int8_t offsets[6]={-1,-1,0,0,1,2};
 int level=(int)s->level+offsets[slot];if(level<1)level=1;if(level>30)level=30;
 s->seed=item_seed(s->seed+0x9e3779b9u);rows->seed[slot]=s->seed;rows->level[slot]=(uint8_t)level;
}
SHOP_FN int premium_valid(const SmithStock *s,const PremiumRows *rows){
 if(!vendor_valid(VENDOR_PREMIUM,s)||s->sold||!rows||rows->reserved)return 0;
 for(unsigned i=0;i<6;i++)if(rows->level[i]<1||rows->level[i]>30)return 0;return 1;
}
SHOP_FN int premium_refresh(SmithStock *s,PremiumRows *rows,unsigned level,int fresh){
 if(!s||!rows||level<1||level>50)return 0;
 if(fresh){s->level=1;s->sold=0;*rows=(PremiumRows){0};for(unsigned i=0;i<6;i++)premium_replace(s,rows,i);}
 else if(!premium_valid(s,rows))return 0;
 while(s->level<level){
  s->level++;
  rows->seed[0]=rows->seed[2];rows->level[0]=rows->level[2];
  rows->seed[1]=rows->seed[3];rows->level[1]=rows->level[3];
  rows->seed[2]=rows->seed[4];rows->level[2]=rows->level[4];
  rows->seed[4]=rows->seed[5];rows->level[4]=rows->level[5];
  premium_replace(s,rows,3);premium_replace(s,rows,5);
 }
 return 1;
}
SHOP_FN int premium_quote(const SmithStock *s,const PremiumRows *rows,unsigned slot,Item *item,unsigned *price){
 if(!premium_valid(s,rows)||slot>=6)return SHOP_INVALID;
 /* The zero-offset native row uses precisely the saved creation level. */
 SmithStock one={.seed=rows->seed[slot],.level=rows->level[slot]};
 return vendor_quote(VENDOR_PREMIUM,&one,3,item,price);
}
SHOP_FN int premium_buy(SmithStock *s,PremiumRows *rows,unsigned slot,Inventory *bag,GameGold *gold,int commit){
 if(!bag||!gold||*gold<0||!items_valid(bag))return SHOP_INVALID;
 Item it;unsigned price;int result=premium_quote(s,rows,slot,&it,&price);if(result!=SHOP_OK)return result;
 if(price>(unsigned)*gold)return SHOP_NO_GOLD;
 Inventory staged=*bag;if(!items_add(&staged,it))return SHOP_NO_ROOM;
 if(commit){*bag=staged;*gold-=price;premium_replace(s,rows,slot);}return SHOP_OK;
}
