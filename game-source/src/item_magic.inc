/* Retail table eligibility, weights and good/evil exclusion; GBA seeded rolls.
 * Only fully supported unique definitions may become candidates. */
#include "item_magic_catalog.h"
#include "../generated/item_magic_catalog.inc"
int item_power_supported(int power){return (power>=0&&power<=11)||(power>=19&&power<=34)||power==14||power==15||power==16||power==17||power==35||power==36||power==37||power==38||power==39||power==42||power==43||power==53||power==59||power==60||power==58||power==55||power==56||power==61||power==45||power==46||power==57||power==52;}
static unsigned magic_type(const Item *it){
 const BaseItemDef *b=item_base(it->kind);if(!b)return 0;
 switch(b->type){case 1:case 2:case 4:return 0x1000;case 3:return 0x10;case 5:return 0x10000;case 6:case 7:case 8:case 9:return 0x100000;case 10:return 0x100;case 12:case 13:return 1;default:return 0;}
}
/* Unique tag consumes the former reserved bit; no ground/Flash ABI growth. */
int item_unique_supported(unsigned id){return id<ITEM_UNIQUE_COUNT;}
const UniqueItemDef *item_unique(const Item *it){
 unsigned id=(it->enchant&127);if(!(it->enchant&0x4000)||(it->enchant&0x3f80)||!id||id>ITEM_UNIQUE_COUNT)return 0;
 const BaseItemDef *b=item_base(it->kind);const UniqueItemDef *u=&item_uniques[id-1];
 return item_unique_supported(id-1)&&b&&b->unique_base==u->base?u:0;
}
#include "../generated/item_art.h"
/* Native cursors change before identification; never alter serialized items. */
unsigned item_sound_kind(const Item *it){if(item_scroll(it))return ITEM_SCROLL_IDENTIFY;const UniqueItemDef *u=item_unique(it);unsigned k=u?unique_sound_kind[u-item_uniques]:255;return k==255?it->kind:k;}
unsigned item_art_index(const Item *it){
 if(item_scroll(it))return ITEM_SCROLL_IDENTIFY;
 const UniqueItemDef *u=item_unique(it);unsigned index=u?unique_art_index[u-item_uniques]:0;
 return index?index:it->kind;
}
/* Cursor dimensions are physical metadata, including before identification. */
static unsigned item_dimension(const Item *it,unsigned axis){
 if(item_scroll(it))return 1;
 const UniqueItemDef *u=item_unique(it);unsigned size=u?unique_art_size[u-item_uniques][axis]:0;
 return size?size:axis?item_defs[it->kind].height:item_defs[it->kind].width;
}
unsigned item_width(const Item *it){return item_dimension(it,0);}
unsigned item_height(const Item *it){return item_dimension(it,1);}
int item_has_power(const Item *it,int power){
 for(unsigned p=0;p<item_power_count(it);p++)if(item_power_at(it,p).power==power)return 1;return 0;
}
unsigned item_strength(const Item *it){return item_has_power(it,65)?0:item_defs[it->kind].strength;}
unsigned item_flags(const Item *it){unsigned f=item_defs[it->kind].flags;return item_has_power(it,68)?f&~ITEM_TWOHAND:f;}
int item_make_unique(Item *it,unsigned id){
 const BaseItemDef *b=item_base(it->kind);if(!item_unique_supported(id)||!b||b->unique_base!=item_uniques[id].base)return 0;
 if(item_is_staff(it))it->flags=it->charges=it->max_charges=0; /* Unique precedes random staff spells in retail. */
 it->enchant=0x4000|(id+1);
 if(item_is_staff(it))for(unsigned p=0;p<item_power_count(it);p++){ItemPower a=item_power_at(it,p);if(a.power==66){it->flags=a.low;it->charges=a.low;it->max_charges=a.high;}}
 item_set_durability(it,item_max_durability(it));return 1;
}
unsigned item_power_count(const Item *it){const UniqueItemDef *u=item_unique(it);return u?u->count:2;}
ItemPower item_power_at(const Item *it,unsigned index){
 const UniqueItemDef *u=item_unique(it);if(u)return index<u->count?u->powers[index]:(ItemPower){-1,0,0};
 const ItemAffix *a=index<2?item_affix(it,index):0;return a?(ItemPower){a->power,a->param1,a->param2}:(ItemPower){-1,0,0};
}
unsigned item_durability(const Item *it){return item_is_staff(it)?(it->durability==255?ITEM_INDESTRUCTIBLE:it->durability):it->durability+((unsigned)it->max_charges<<8);}
void item_set_durability(Item *it,unsigned value){it->durability=(uint8_t)value;if(!item_is_staff(it))it->max_charges=value>>8;}
unsigned item_original_durability(const Item *it){
 if(it->kind>=ITEM_COUNT)return 0;unsigned value=item_defs[it->kind].durability;
 for(unsigned p=0;p<item_power_count(it);p++){
  ItemPower a=item_power_at(it,p);int amount=item_power_value(it,p);
  if(a.power==37)return ITEM_INDESTRUCTIBLE;
  if(a.power==64)value=a.low;
  if(a.power==35)value+=value*amount/100;
  if(a.power==36){value-=value*amount/100;if(!value)value=1;}
 }return value;
}
unsigned item_max_durability(const Item *it){
 if((it->flags&ITEM_PHYSICAL)&&!item_is_spell(it)){unsigned max=item_is_staff(it)?it->wear_loss&255:it->wear_loss;return item_is_staff(it)&&max==255?ITEM_INDESTRUCTIBLE:max;}
 unsigned max=item_original_durability(it);return max==ITEM_INDESTRUCTIBLE?max:max>it->wear_loss?max-it->wear_loss:0;
}
#include "item_physical.inc"
/* Drop durability is rolled after bonuses, as retail SetupAllItems/ItemRndDur. */
static void item_apply_durability(Item *it){
 unsigned max=item_max_durability(it);
 for(unsigned p=0;p<item_power_count(it);p++)if(item_power_at(it,p).power>=35&&item_power_at(it,p).power<=37){
  item_set_durability(it,max==ITEM_INDESTRUCTIBLE?max:max/4+1+item_seed(it->seed^0x41u)%(max/2?max/2:1));return;
 }
}
const ItemAffix *item_affix(const Item *it,unsigned suffix){
 if(it->enchant&0x4000)return 0;unsigned n=(it->enchant>>(suffix?7:0))&127;
 return n&&n<=(suffix?ITEM_SUFFIX_COUNT:ITEM_PREFIX_COUNT)?(suffix?item_suffixes:item_prefixes)+n-1:0;
}
int item_enchant_valid(const Item *it){
 if(!it->enchant)return 1;
 if(it->enchant&0x4000)return item_unique(it)!=0;
 if(!(it->enchant&0x3fff)||!magic_type(it))return 0;
 if(item_is_staff(it)&&item_spell(it)&&(it->enchant&0x3f80))return 0; /* Spell staves only take a prefix. */
 unsigned alignment=0;
 for(unsigned s=0;s<2;s++){
  unsigned id=(it->enchant>>(s?7:0))&127;const ItemAffix *a=item_affix(it,s);
  if(!id)continue;
  if(!a||!item_power_supported(a->power)||!(a->types&magic_type(it)))return 0;
  if(a->power==15&&(!item_is_staff(it)||!item_spell(it)||(it->flags&128)))return 0;
  alignment|=a->alignment;
 }return alignment!=17; /* GOE_GOOD | GOE_EVIL */
}
int item_unidentified(const Item *it){return (it->enchant&0x3fff)&&!(it->enchant&0x8000);}
static unsigned magic_roll(uint32_t *state,unsigned n){*state=item_seed(*state+0x9e3779b9u);return n?*state%n:0;}
static unsigned pick_affix(const Item *it,unsigned suffix,unsigned low,unsigned high,unsigned good,unsigned alignment,uint32_t *state){
 const ItemAffix *rows=suffix?item_suffixes:item_prefixes;unsigned count=suffix?ITEM_SUFFIX_COUNT:ITEM_PREFIX_COUNT,total=0,type=magic_type(it);
 for(unsigned pass=0;pass<2;pass++){
  unsigned pick=pass?magic_roll(state,total):0;if(pass&&!total)return 0;
  for(unsigned i=0;i<count;i++){
   const ItemAffix *a=&rows[i];
   if(!item_power_supported(a->power)||!(a->types&type)||a->min_level<low||a->min_level>high||(good&&!a->good)||((alignment|a->alignment)==17))continue;
   if(a->power==15&&!item_spell(it))continue;
   unsigned weight=1+(!suffix&&a->double_weight);
   if(!pass)total+=weight;else if(pick<weight)return i+1;else pick-=weight;
  }
 }return 0;
}
void item_generate_magic(Item *it,unsigned level,int only_good){
 if(!magic_type(it)||it->enchant||(item_is_staff(it)&&(it->flags&128)))return;
 uint32_t state=it->seed^0xd1ab109u;unsigned type=magic_type(it);
 if(!only_good&&type!=1&&type!=0x100&&magic_roll(&state,100)>10&&magic_roll(&state,100)>level)return;
 /* Retail candidate filtering and highest eligible ID; quest-only rows0..8
  * never appear in the random pool. GBA permits repeat uniques, as multiplayer. */
 if(magic_roll(&state,100)<=1){
  const BaseItemDef *b=item_base(it->kind);int chosen=-1;
  for(unsigned i=9;i<ITEM_UNIQUE_COUNT;i++)if(item_unique_supported(i)&&item_uniques[i].base==b->unique_base&&item_uniques[i].min_level<=level)chosen=i;
  if(chosen>=0&&item_make_unique(it,(unsigned)chosen))return;
 }
 if(type==0x100&&item_spell(it)){
  if(only_good||magic_roll(&state,10)==0){
   it->enchant=pick_affix(it,0,0,level,only_good,0,&state);
   unsigned multiplier=item_charge_multiplier(it);it->charges*=multiplier;it->max_charges*=multiplier;
  }return;
 }
 unsigned pre=magic_roll(&state,4),post=magic_roll(&state,3);
 if(pre&& !post){if(magic_roll(&state,2))post=1;else pre=0;}
 unsigned good=only_good||magic_roll(&state,3)!=0,low=level/2,alignment=0;if(low>25)low=25;
 if(!pre){unsigned id=pick_affix(it,0,low,level,good,0,&state);it->enchant=id;if(id)alignment=item_prefixes[id-1].alignment;}
 if(post)it->enchant|=pick_affix(it,1,low,level,good,alignment,&state)<<7;
 item_apply_durability(it);
}
int item_power_value(const Item *it,unsigned index){
 ItemPower a=item_power_at(it,index);if(a.power<0)return 0;
 int value=a.low;if(a.high>=a.low)value+=(int)(item_seed(it->seed^(item_unique(it)?0x771ab1u+index*0x9e3779b9u:index?0x5aff1u:0x9aff1u))%(unsigned)(a.high-a.low+1));
 int p=a.power;return p==1||p==3||p==5||p==7||p==20||p==22||p==24||p==26||p==28||p==30||p==32||p==34||p==39?-value:value;
}
int item_affix_value(const Item *it,unsigned suffix){return item_affix(it,suffix)?item_power_value(it,suffix):0;}
int item_affix_secondary(const Item *it,unsigned suffix){
 const ItemAffix *a=item_affix(it,suffix);if(!a|| (a->power!=4&&a->power!=5))return 0;
 static const uint8_t thresholds[]={20,36,51,66,81,96,111,126,151};
 static const uint8_t low[]={1,6,11,16,21,31,41,51,76},high[]={5,10,15,20,30,40,50,75,100};
 unsigned index=0;if(a->power==5)index=a->param1==50;else for(unsigned i=0;i<9;i++)if(a->param1==thresholds[i])index=i;
 int value=low[index]+item_seed(it->seed^(suffix?0x52b17u:0x92b17u))%(high[index]-low[index]+1);return a->power==5?-value:value;
}
int item_bonus(const Item *it,unsigned category){
 if(!it->enchant||item_unidentified(it)||!item_enchant_valid(it))return 0;int total=0;
 for(unsigned s=0;s<item_power_count(it);s++){ItemPower a=item_power_at(it,s);int p=a.power,value=item_power_value(it,s),match=0;
  if(category==0)match=p>=2&&p<=5;
  else if(category==1)match=p==61;
  else if(category==2)match=p==6||p==7;
  else if(category>=3&&category<=6)match=p==19+2*(int)(category-3)||p==20+2*(int)(category-3)||p==27||p==28;
  else if(category==7)match=p==31||p==32;
  else if(category==8)match=p==33||p==34;
  else if(category==9){match=p==0||p==1;if(p==4||p==5)total+=item_affix_secondary(it,s);}
  else if(category>=10&&category<=12)match=p==(int)category-2||p==11;
  else if(category==13)match=p==29||p==30;
  else if(category==14)match=p==55;
  else if(category==15)match=p==56;
  else if(category==16)match=p==58;
  else if(category==17){if(p==53)total=1;}
  else if(category==18)match=p==59;
  else if(category==19){if(p==60)total=1;}
  else if(category==20)match=p==14;
  else if(category==21)match=p==38||p==39;
  if(match){if(category==14||category==15||category==16||category==18){if(value>total)total=value;}else total+=value;}
 }return total;
}
unsigned item_weapon_roll(uint32_t roll,unsigned attribute_bonus){
 int damage=item_effects.weapon_low+roll%(item_effects.weapon_high-item_effects.weapon_low+1);
 damage+=damage*item_effects.damage_percent/100;damage+=item_effects.damage_flat+(int)attribute_bonus;
 return damage>0?(unsigned)damage:1;
}
void item_name(const Item *it,char out[64]){
 if(item_is_spell(it)){
  unsigned n=0;const char *parts[2]={item_scroll(it)?"SCROLL OF ":"BOOK OF ",game_spell_name(item_spell(it))};
  for(unsigned i=0;i<2;i++)for(const char *p=parts[i];*p&&n<63;p++)out[n++]=*p;
  out[n]=0;return;
 }
 unsigned n=0;const char *pieces[5]={0,item_defs[it->kind].name,0,0,0};
 const ItemAffix *pre=item_affix(it,0),*suf=item_affix(it,1);
 if(!item_unidentified(it)){if(pre){pieces[0]=pre->name;pieces[1]=" ";pieces[2]=item_defs[it->kind].name;}if(suf){pieces[3]=" OF ";pieces[4]=suf->name;}}
 const UniqueItemDef *unique=item_unique(it);if(unique&&!item_unidentified(it)){for(unsigned i=0;i<5;i++)pieces[i]=0;pieces[0]=unique->name;}
 for(unsigned i=0;i<5;i++)if(pieces[i])for(const char *s=pieces[i];*s&&n<63;s++){char c=*s;if(c=='\'')continue;out[n++]=c>='a'&&c<='z'?c-32:c;}
 out[n]=0;
}
/* Physical charges are rolled once, independent of identification. */
unsigned item_charge_multiplier(const Item *it){
 const ItemAffix *a=item_affix(it,0);
 return item_is_staff(it)&&item_spell(it)&&a&&a->power==15?(unsigned)a->param1:1;
}
/* Ground record has independent enchant/charge fields and explicit zero padding. */
Item item_from_drop(const Drop *d){
 return (Item){.seed=d->seed,.kind=d->kind,.durability=d->durability,.flags=d->flags,.charges=d->charges,.max_charges=d->max_charges,.enchant=d->enchant,.wear_loss=d->wear_loss};
}
Drop item_to_drop(const Item *it,unsigned x,unsigned y){
 return (Drop){.x=x,.y=y,.active=1,.kind=it->kind,.seed=it->seed,.durability=it->durability,.flags=it->flags,.charges=it->charges,.max_charges=it->max_charges,.enchant=it->enchant,.wear_loss=it->wear_loss};
}
int game_identify_item(Game *g,unsigned index){
 if(g->hp<=0)return INV_BUSY;
 Inventory *bag=game_inventory();if(index>=ITEM_SLOTS||!item_valid(&bag->items[index])||!item_unidentified(&bag->items[index]))return INV_NO_IDENTIFY;
 for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_SCROLL_IDENTIFY&&item_valid(&bag->items[i])){
  bag->items[index].enchant|=0x8000;bag->items[i]=(Item){.kind=ITEM_EMPTY};game_items_update(g);return INV_OK;
 }
 return INV_NO_SCROLL;
}

#include "item_effects.inc"
#include "item_durability.inc"
/* Native GetItemBonus for stores does not run unique selection. */
void item_generate_store_magic(Item *it,unsigned low,unsigned high){
 if(!magic_type(it)||it->enchant)return;
 uint32_t state=it->seed^0x570aeu;
 if(item_is_staff(it)&&item_spell(it)){
  it->enchant=pick_affix(it,0,0,high,1,0,&state);
  unsigned m=item_charge_multiplier(it);it->charges*=m;it->max_charges*=m;
 }else{
  unsigned pre=magic_roll(&state,4),post=magic_roll(&state,3),alignment=0;
  if(pre&&!post){if(magic_roll(&state,2))post=1;else pre=0;}
  if(!pre){unsigned id=pick_affix(it,0,low,high,1,0,&state);it->enchant=id;if(id)alignment=item_prefixes[id-1].alignment;}
  if(post)it->enchant|=pick_affix(it,1,low,high,1,alignment,&state)<<7;
 }
 if(it->enchant)it->enchant|=0x8000;
 item_set_durability(it,item_max_durability(it));
}
