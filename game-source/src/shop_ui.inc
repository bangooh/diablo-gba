/* Local store menus; all co-op mutations wait for an acknowledged command. */
static unsigned shop_slot,shop_result,shop_npc,shop_action,shop_choice,shop_return;
static uint16_t shop_queued;
static uint8_t shop_detail_offset __attribute__((section(".iwram.info")))=0;
static uint32_t shop_quoted_token;
static UI_FN unsigned shop_pending_command(void){return shop_queued;}
static UI_FN uint32_t shop_pending_token(void){return shop_queued?shop_quoted_token:0;}
static UI_FN void shop_cancel_pending(void){shop_queued=0;shop_quoted_token=0;}
enum { SHOP_ACTION_SELL=VENDOR_COUNT,SHOP_ACTION_REPAIR,SHOP_ACTION_RECHARGE,SHOP_ACTION_IDENTIFY };
static const char *const shop_labels[]={"Buy basic items","Buy premium items","Buy items","Buy items","View item / 50 gold","Sell items","Repair items","Recharge staves","Identify an item","B: Say goodbye","Talk"};
static UI_FN unsigned shop_option(unsigned row){
 static const uint8_t smith[]={10,0,1,5,6,9},witch[]={10,2,5,7,9},healer[]={10,3,9},cain[]={10,8,9},boy[]={10,4,9};
 return shop_npc==0?smith[row]:shop_npc==3?witch[row]:shop_npc==6?healer[row]:shop_npc==7?cain[row]:boy[row];
}
static UI_FN unsigned shop_options(void){return shop_npc==0?6:shop_npc==3?5:3;}
static UI_FN SmithStock *shop_stock(void){return shop_action?&town_other[shop_action-1]:&town_smith;}
static UI_FN unsigned shop_command_action(void){
 return shop_action<VENDOR_COUNT?shop_action:shop_action==SHOP_ACTION_SELL?(shop_npc==3?GAME_SHOP_SELL_WITCH:GAME_SHOP_SELL_SMITH):shop_action+1;
}
static UI_FN unsigned shop_count(void){return shop_action<VENDOR_COUNT?vendor_count(shop_action,shop_stock()):ITEM_SLOTS+(shop_action==SHOP_ACTION_SELL&&shop_npc==3?10:0);}
/* Modal-only cache in unused loading scratch, beyond actor decoding and the
 * dialogue strip/map. No permanent EWRAM allocation and no cached purchases.
 * Authoritative transactions still reconstruct and validate their quote. */
typedef struct {uint32_t key,known;Item items[20];unsigned prices[20];uint8_t result[20];} ShopQuoteCache;
#define SHOP_QUOTE_CACHE ((ShopQuoteCache*)((uint8_t*)load_buffer+12288))
_Static_assert(12288+sizeof(ShopQuoteCache)<=sizeof(load_buffer),"shop scratch capacity");
static uint8_t shop_cache_valid __attribute__((section(".iwram.info")))=0;
static UI_FN int shop_quote(unsigned slot,Item *item,unsigned *price){
 if(shop_action>=VENDOR_COUNT||slot>=20||!game.area||game.hp<=0||game.gold<0)
  return game_shop_quote(&game,shop_command_action(),slot,item,price);
 ShopQuoteCache *cache=SHOP_QUOTE_CACHE;
 uint32_t key=game_shop_hash(2166136261u)^shop_action;
 if(!shop_cache_valid||cache->key!=key){
  cache->key=key;cache->known=0;shop_cache_valid=1;
  if(shop_action==VENDOR_SMITH){
   unsigned n=smith_stock_list(shop_stock(),cache->items);
   for(unsigned i=0;i<n;i++){cache->prices[i]=item_base(cache->items[i].kind)->value;cache->result[i]=(shop_stock()->sold&(1u<<i))?SHOP_SOLD:SHOP_OK;cache->known|=1u<<i;}
  }
 }
 if(!(cache->known&(1u<<slot))){
  cache->result[slot]=(uint8_t)game_shop_quote(&game,shop_command_action(),slot,&cache->items[slot],&cache->prices[slot]);
  cache->known|=1u<<slot;
 }
 if(cache->result[slot]==SHOP_OK){*item=cache->items[slot];*price=cache->prices[slot];}
 return cache->result[slot];
}
static UI_FN unsigned shop_available(void){
 unsigned n=shop_count(),left=0;Item it;unsigned price;
 if(shop_action<VENDOR_COUNT){for(unsigned i=0;i<n;i++)if(!(shop_stock()->sold&(1u<<i)))left++;return left;}
 for(unsigned i=0;i<n;i++)if(shop_quote(i,&it,&price)==SHOP_OK)left++;
 return left;
}
/* Only quoted, visible entries contribute to list length and page position.
 * Sold slots and gaps in the inventory must not shift the scrollbar. */
static UI_FN __attribute__((minsize)) unsigned shop_detail_format(const Item *item,char text[26][64],unsigned *name_rows);
static UI_FN unsigned shop_scroll_position(unsigned *first){
 if(menu_mode==8){Item item;unsigned price,names;char text[26][64];*first=shop_detail_offset;return shop_quote(shop_slot,&item,&price)==SHOP_OK?shop_detail_format(&item,text,&names):0;}
 unsigned total=0,ordinal=0;Item item;unsigned price;
 for(unsigned i=0,n=shop_count();i<n;i++)if(shop_quote(i,&item,&price)==SHOP_OK){if(i<shop_slot)ordinal++;total++;}
 *first=ordinal/4*4;return total;
}
static UI_FN void shop_move(int direction){
 unsigned n=shop_count();if(!n)return;Item it;unsigned price;
 for(unsigned i=0;i<n;i++){shop_slot=direction>0?(shop_slot+1)%n:(shop_slot+n-1)%n;if(shop_quote(shop_slot,&it,&price)==SHOP_OK)return;}
}
static UI_FN void shop_submit(unsigned action,unsigned slot){
 if(coop_active){shop_queued=(uint16_t)((action<<6|slot)+1);shop_result=0;}
 else shop_result=game_shop_command(&game,action,slot,shop_quoted_token);
 menu_mode=9;
}
static UI_FN void shop_list(void){
 shop_cache_valid=0;shop_slot=0;shop_detail_offset=shop_result=shop_return=0;Item it;unsigned price;
 if(shop_quote(0,&it,&price)!=SHOP_OK)shop_move(1);menu_mode=7;
}
static UI_FN void shop_acknowledge(unsigned result){
 if(!shop_queued)return;unsigned action=(shop_queued-1)>>6;shop_queued=0;shop_quoted_token=0;
 if(action==GAME_SHOP_WIRT_VIEW&&result==SHOP_OK){shop_list();return;}
 shop_result=result;shop_return=action==GAME_SHOP_WIRT_VIEW;menu_mode=9;
}
static UI_FN int shop_open_npc(unsigned npc){
 if(shop_queued||!game.area||game.hp<=0||(npc!=0&&npc!=3&&npc!=5&&npc!=6&&npc!=7)||!smith_stock_valid(&town_smith))return 0;
 shop_cache_valid=0;shop_npc=npc;shop_choice=shop_return=0;shop_action=0;menu_mode=10;return 1;
}
/* Four original-style list entries; two centered name rows per entry. All
 * strings live in dialogue_draw's stack until its canvas has been published. */
static UI_FN void shop_name_lines(DialogueLine *lines,unsigned *count,const char *text,unsigned y,unsigned color,unsigned selected){
 for(unsigned row=0;row<2&&*text;row++){
  unsigned n=0,last=0,width=0;
  while(text[n]){unsigned left,w=dialogue_glyph((uint8_t)text[n],&left);if(width+w>190)break;if(text[n]==' ')last=n;width+=w+1;n++;}
  if(text[n]&&last)n=last;
  width=dialogue_width(text,n);
  lines[(*count)++]=(DialogueLine){text,(uint16_t)n,(uint8_t)((240-width)/2),(uint8_t)(y+row*10),(uint8_t)color,(uint8_t)(selected&&!row)};
  text+=n;while(*text==' ')text++;
 }
}
static UI_FN void shop_number(char *text,const char *label,unsigned value){
 unsigned n=0;while(*label)text[n++]=*label++;n+=info_uint(text+n,value);text[n]=0;
}
static UI_FN unsigned shop_detail_collect(char text[26][64],unsigned count){
 for(unsigned row=0;row<info_count&&count<24;row++,count++){
  unsigned n=0;do{text[count][n]=info_lines[row][n];}while(info_lines[row][n++]);
 }
 return count;
}
/* Match bottom-panel order, with complete labels and a separate row for each
 * property. Use transient rows plus scrolling instead of dropping affixes or
 * requirements at the five-row information-panel limit. */
static UI_FN __attribute__((minsize)) unsigned shop_detail_format(const Item *item,char text[26][64],unsigned *name_rows){
 const ItemDef *d=&item_defs[item->kind];unsigned count=0;
 char name[64];item_name(item,name);const char *at=name;
 /* The detail canvas is 216px wide; the bottom-panel 22-character split
  * is unrelated to this layout. Preserve whole words only when necessary. */
 while(*at&&count<3){
  unsigned n=0,last=0,width=0;
  while(at[n]){unsigned left,w=dialogue_glyph((uint8_t)at[n],&left);if(width+w>216)break;if(at[n]==' ')last=n;width+=w+1;n++;}
  if(at[n]&&last)n=last;if(!n)n=1;
  for(unsigned i=0;i<n;i++)text[count][i]=at[i];text[count++][n]=0;
  at+=n;while(*at==' ')at++;
 }
 *name_rows=count;
 if(d->slot>=EQUIP_COUNT||item_unidentified(item)){
  info_reset();info_item(item,2);unsigned skip=info_name_style&7;
  for(unsigned row=skip;row<info_count&&count<24;row++,count++){
   unsigned n=0;do{text[count][n]=info_lines[row][n];}while(info_lines[row][n++]);
  }
  return count;
 }
 info_reset();
 if(d->slot==EQUIP_HAND)info_range("DAMAGE: ",item_weapon_low(item),item_weapon_high(item),'-');
 else if(d->ac_high||item_ac(item)){int ac=item_ac(item);info_value(ac<0?"ARMOR: -":"ARMOR: ",ac<0?-ac:ac);}
 count=shop_detail_collect(text,count);info_reset();
 if(item_max_durability(item)==ITEM_INDESTRUCTIBLE)info_add("INDESTRUCTIBLE");
 else if(d->durability)info_range("DURABILITY: ",item_durability(item),item_max_durability(item),'/');
 count=shop_detail_collect(text,count);info_reset();
 if(item_is_staff(item)&&item_spell(item))info_range("CHARGES: ",item->charges,item->max_charges,'/');
 count=shop_detail_collect(text,count);
 for(unsigned i=0;i<item_power_count(item);i++){info_reset();info_affix(item,i);count=shop_detail_collect(text,count);}
 info_reset();
 if(item_strength(item))info_value("REQUIRED STRENGTH: ",item_strength(item));
 if(item_magic(item))info_value("REQUIRED MAGIC: ",item_magic(item));
 if(d->dexterity)info_value("REQUIRED DEXTERITY: ",d->dexterity);
 return shop_detail_collect(text,count);
}
static UI_FN void shop_dialogue(DialogueLine *lines,unsigned *count,char text[26][64]){
 if(menu_mode==10){
  dialogue_center(lines,count,"Would you like to:",36,HUD_GOLD,0);
  for(unsigned i=0;i<shop_options();i++){
   unsigned option=shop_option(i);
   const char *label=option==10?dialogue_talk_labels[shop_npc]:shop_labels[option];
   dialogue_center(lines,count,label,option==9?140:52+i*16,option==10?HUD_BLUE:HUD_WHITE,shop_choice==i);
  }
  return;
 }
 shop_number(text[25],"Your gold: ",game.gold);
 dialogue_center(lines,count,text[25],124,HUD_GOLD,0);
 const char *footer="B: Back";
 if(menu_mode==7){
  unsigned n=shop_count(),ordinal=0;Item item;unsigned price;
  for(unsigned i=0;i<shop_slot&&i<n;i++)if(shop_quote(i,&item,&price)==SHOP_OK)ordinal++;
  unsigned first=ordinal/4*4,row=0,index=0;
  for(unsigned i=0;i<n&&row<4;i++)if(shop_quote(i,&item,&price)==SHOP_OK){
   if(index++<first)continue;
   item_name(&item,text[row]);shop_name_lines(lines,count,text[row],34+row*20,item_name_color(&item,1),i==shop_slot);row++;
  }
  if(!row)dialogue_center(lines,count,"No items remain",68,HUD_WHITE,0);
  else footer="B: Back    A: View";
 }else if(menu_mode==8){
  Item item;unsigned price;
  int valid=shop_quoted_token==game_shop_quote_token(&game,shop_command_action(),shop_slot)&&shop_quote(shop_slot,&item,&price)==SHOP_OK;
  if(valid){
   unsigned name_rows,total=shop_detail_format(&item,text,&name_rows),limit=total>5?total-5:0;
   if(shop_detail_offset>limit)shop_detail_offset=limit;
   for(unsigned row=0;row<5&&shop_detail_offset+row<total;row++){unsigned i=shop_detail_offset+row;dialogue_center(lines,count,text[i],36+row*12,i<name_rows?item_name_color(&item,1):HUD_WHITE,0);}
   /* Five description rows, one empty row, then the centered quoted price. */
   shop_number(text[24],"Price: ",price);dialogue_center(lines,count,text[24],108,HUD_WHITE,0);
   footer=shop_action<VENDOR_COUNT?"B: Back    A: Buy":shop_action==SHOP_ACTION_SELL?"B: Back    A: Sell":shop_action==SHOP_ACTION_REPAIR?"B: Back    A: Repair":shop_action==SHOP_ACTION_RECHARGE?"B: Back    A: Recharge":"B: Back    A: Identify";
  }else dialogue_center(lines,count,"Item unavailable",68,HUD_WHITE,0);
 }else{
  const char *message=shop_queued?"Waiting for party":shop_result==SHOP_OK?"Done":shop_result==SHOP_NO_GOLD?"Not enough gold":shop_result==SHOP_NO_ROOM?"Not enough room":shop_result==SHOP_GOLD_FULL?"Gold limit reached":"Item unavailable";
  dialogue_center(lines,count,message,68,HUD_WHITE,0);
 }
 if(!shop_queued)dialogue_center(lines,count,footer,140,HUD_WHITE,0);
}
static UI_FN int shop_input(unsigned pressed){
 if(shop_queued)return 0;
 if(!game.area||game.hp<=0||(pressed&KEY_START))return 1;
 if(menu_mode==10){
  if(pressed&KEY_B)return 1;
  if(pressed&KEY_UP)shop_choice=(shop_choice+shop_options()-1)%shop_options();else if(pressed&KEY_DOWN)shop_choice=(shop_choice+1)%shop_options();
  if(pressed&KEY_A){shop_action=shop_option(shop_choice);if(shop_action==9)return 1;
   if(shop_action==10){dialogue_topics_open(shop_npc);return 0;}
   if(shop_action==VENDOR_BOY){
    shop_result=game_shop_check(&game,GAME_SHOP_WIRT_VIEW,0);shop_return=1;
    if(shop_result!=SHOP_OK){menu_mode=9;return 0;}
    shop_quoted_token=game_shop_quote_token(&game,GAME_SHOP_WIRT_VIEW,0);shop_submit(GAME_SHOP_WIRT_VIEW,0);
    if(!coop_active&&shop_result==SHOP_OK)shop_list();return 0;
   }
   shop_list();
  }return 0;
 }
 if(menu_mode==9){if(pressed&(KEY_A|KEY_B)){if(shop_return){shop_return=0;menu_mode=10;return 0;}Item it;unsigned price;if(shop_quote(shop_slot,&it,&price)!=SHOP_OK)shop_move(1);menu_mode=7;}return 0;}
 if(pressed&KEY_B){menu_mode=menu_mode==8?7:10;return 0;}
 if(menu_mode==8){
  if(pressed&(KEY_UP|KEY_DOWN)){Item item;unsigned price,names;char text[26][64];
   if(shop_quote(shop_slot,&item,&price)==SHOP_OK){unsigned n=shop_detail_format(&item,text,&names),limit=n>5?n-5:0;
    if((pressed&KEY_UP)&&shop_detail_offset)shop_detail_offset--;else if((pressed&KEY_DOWN)&&shop_detail_offset<limit)shop_detail_offset++;
   }
  }
  if(pressed&KEY_A)shop_submit(shop_command_action(),shop_slot);return 0;
 }
 if(pressed&KEY_UP)shop_move(-1);else if(pressed&KEY_DOWN)shop_move(1);
 if((pressed&KEY_A)&&shop_available()){shop_quoted_token=game_shop_quote_token(&game,shop_command_action(),shop_slot);shop_detail_offset=0;menu_mode=8;}
 return 0;
}
