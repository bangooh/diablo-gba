/* Stage64: retail Hidden/Gloomy/Weird modify physical item fields, not affixes.
 * Legacy wear_loss records remain untouched until the first physical change.
 * With flags bit6 set, non-staff wear_loss=max durability, charges=base AC or
 * maximum damage. Tagged non-staff armor uses flags bit7 for a negative AC;
 * charges holds its magnitude, including unique AC curses. Staff bit7 remains
 * the starter-staff flag. Staff wear_loss packs max durability/base maximum damage;
 * charges, max_charges and spell bits remain independent. No record growth.
 * Existing byte combat/staff storage bounds are saturated, never wrapped into
 * zero damage or the staff255 indestructible sentinel. */
static unsigned item_physical_type(const Item *it){const BaseItemDef *b=item_base(it->kind);return b?b->type:0;}
static int item_physical_weapon(unsigned type){return (type>=1&&type<=4)||type==10;}
/* SETAC/AC_CURSE/SETDAM are generated physical fields in retail; they apply
 * even before identification. Affix percentages remain identification-gated. */
static int item_native_ac(const Item *it){
 if(it->kind>=ITEM_COUNT)return 0;
 const ItemDef *d=&item_defs[it->kind];int ac=d->ac_low+item_seed(it->seed)%(d->ac_high-d->ac_low+1);
 const UniqueItemDef *u=item_unique(it);
 if(u){for(unsigned i=0;i<u->count;i++){
  if(u->powers[i].power==75)ac=item_power_value(it,i);
  if(u->powers[i].power==79)ac-=item_power_value(it,i);
 }}
 return ac;
}
static unsigned item_native_weapon(const Item *it,unsigned high){
 if(it->kind>=ITEM_COUNT)return 0;
 unsigned damage=high?item_defs[it->kind].high:item_defs[it->kind].low;
 const UniqueItemDef *u=item_unique(it);
 if(u)for(unsigned i=0;i<u->count;i++)if(u->powers[i].power==63)damage=high?u->powers[i].high:u->powers[i].low;
 return damage;
}
unsigned item_weapon_low(const Item *it){return item_native_weapon(it,0);}
unsigned item_weapon_high(const Item *it){
 if((it->flags&ITEM_PHYSICAL)&&item_physical_weapon(item_physical_type(it)))return item_is_staff(it)?it->wear_loss>>8:it->charges;
 return item_native_weapon(it,1);
}
int item_ac(const Item *it){
 unsigned type=item_physical_type(it);
 int ac=(it->flags&ITEM_PHYSICAL)&&type>=5&&type<=9?((it->flags&ITEM_PHYSICAL_NEGATIVE)?-(int)it->charges:it->charges):item_native_ac(it);
 int pct=item_bonus(it,2),delta=ac*pct/100;if(pct&&!delta)delta=pct>0?1:-1;
 return ac+delta;
}
static void item_physical_begin(Item *it){
 if(it->flags&ITEM_PHYSICAL)return;
 unsigned max=item_max_durability(it),type=item_physical_type(it);
 int base=item_physical_weapon(type)?(int)item_native_weapon(it,1):item_native_ac(it);
 if(base<-255)base=-255;if(base>255)base=255;
 if(item_is_staff(it))it->wear_loss=(max==ITEM_INDESTRUCTIBLE?255:max)|((unsigned)base<<8);
 else{it->wear_loss=max;it->charges=base<0?-base:base;if(base<0)it->flags|=ITEM_PHYSICAL_NEGATIVE;}
 it->flags|=ITEM_PHYSICAL;
}
static void item_physical_max(Item *it,unsigned max){
 if(item_is_staff(it))it->wear_loss=(it->wear_loss&0xff00u)|max;else it->wear_loss=max;
}
/* Public entrypoint is called only by the consumed-shrine transaction. */
void game_item_shrine(Game *g,unsigned type,uint32_t seed){
 Inventory *bag=game_inventory();unsigned eligible=0;
 if(type==1){
  for(unsigned i=0;i<ITEM_SLOTS;i++){
   Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT||it->pos<BAG_CELLS||it->pos>=BAG_CELLS+EQUIP_COUNT)continue;
   unsigned max=item_max_durability(it);if(!max||max==ITEM_INDESTRUCTIBLE)continue;
   item_physical_begin(it);unsigned cap=item_is_staff(it)?254:ITEM_INDESTRUCTIBLE-1,now=item_durability(it);
   max=max>cap-10?cap:max+10;now=now>max-10?max:now+10;
   item_physical_max(it,max);item_set_durability(it,now);eligible++;
  }
  if(eligible){unsigned chosen=item_seed(seed^0x48494444u)%eligible;
   for(unsigned slot=0;slot<EQUIP_COUNT;slot++)for(unsigned i=0;i<ITEM_SLOTS;i++){
    Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT||it->pos!=BAG_CELLS+slot)continue;
    unsigned max=item_max_durability(it);if(!max||max==ITEM_INDESTRUCTIBLE)continue;
    if(chosen){chosen--;continue;}
    unsigned now=item_durability(it);item_physical_max(it,max>20?max-20:1);item_set_durability(it,now>20?now-20:1);
    goto hidden_done;
   }
  }
 hidden_done:;
 }else if(type==2||type==3){
  for(unsigned i=0;i<ITEM_SLOTS;i++){
   Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT)continue;
   unsigned kind=item_physical_type(it),weapon=item_physical_weapon(kind);
   if(!weapon&&!(type==2&&kind>=5&&kind<=9))continue;
   item_physical_begin(it);
   int base=item_is_staff(it)?it->wear_loss>>8:((!weapon&&(it->flags&ITEM_PHYSICAL_NEGATIVE))?-(int)it->charges:it->charges);
   if(weapon){int low=(int)item_weapon_low(it);if(type==2){if(base>low)base--;}else if(base<255)base++;}
   else base=base>253?255:base+2;
   if(item_is_staff(it))it->wear_loss=(it->wear_loss&255)|(base<<8);else{it->charges=base<0?-base:base;if(!weapon)it->flags=(it->flags&~ITEM_PHYSICAL_NEGATIVE)|(base<0?ITEM_PHYSICAL_NEGATIVE:0);}
  }
 }
 game_items_update(g);
}
