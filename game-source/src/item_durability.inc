/* Stateless rolls are deterministic for each simulation tick/owner/event.
 * PC local-only mutation is adapted to lockstep: every peer updates the owner. */
void game_wear_equipment(Game *g,unsigned event){
 if(g->area||g->hp<=0||event>WEAR_HIT)return;
 if(coop_active&&(coop_cpu_mask&(1u<<coop_turn))&&(event==WEAR_MELEE||event==WEAR_BOW))return;
 uint32_t roll=item_seed(g->ticks^((coop_active?coop_turn:0)*0x9e3779b9u)^((event+1)*0x632be5abu));
 unsigned slot;
 if(event==WEAR_MELEE||event==WEAR_BOW){
  if(roll%(event==WEAR_BOW?40:30))return;
  slot=EQUIP_HAND;if(!(item_effects.usable&(1u<<slot))){if(event==WEAR_BOW)return;slot=EQUIP_SHIELD;}
 }else if(event==WEAR_BLOCK){if(roll%10)return;slot=EQUIP_SHIELD;}
 else{
  if(!(roll%4))return;
  unsigned mask=item_effects.usable;
  if(!(mask&((1u<<EQUIP_HEAD)|(1u<<EQUIP_CHEST))))return;
  slot=!(mask&(1u<<EQUIP_HEAD))?EQUIP_CHEST:!(mask&(1u<<EQUIP_CHEST))?EQUIP_HEAD:item_seed(roll)%3?EQUIP_CHEST:EQUIP_HEAD;
 }
 if(!(item_effects.usable&(1u<<slot)))return;
 Inventory *bag=game_inventory();
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  Item *it=&bag->items[i];if(it->kind>=ITEM_COUNT||it->pos!=BAG_CELLS+slot)continue;
  unsigned durability=item_durability(it);if(!durability||durability==ITEM_INDESTRUCTIBLE)return;
  item_set_durability(it,durability-1);if(durability>1)return;
  *it=(Item){.kind=ITEM_EMPTY};game_items_update(g);
  if(event==WEAR_MELEE||event==WEAR_BOW){g->attack_wait=g->attack_duration=0;g->swing_target=-1;if(g->attack_queued!=2)g->attack_queued=0;}
  return;
 }
}
