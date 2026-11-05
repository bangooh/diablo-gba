/* Atomic cursor placement: the live item remains valid until a destination
 * succeeds. Cancel/save/link interruption cannot lose or duplicate an item. */
static uint8_t drag_index[4];
static uint32_t drag_signature[4];
static unsigned drag_owner(void){return coop_active?coop_turn:0;}
static uint32_t drag_key(const Game *g,unsigned index){
 if(index>=ITEM_SLOTS)return game_belt_kind(g,index-ITEM_SLOTS);
 const Item *it=&LIVE_INVENTORY.items[index];if(it->kind>=ITEM_COUNT)return 0;
 const uint8_t *b=(const uint8_t*)it;uint32_t h=2166136261u;
 for(unsigned i=0;i<sizeof(*it);i++)h=(h^b[i])*16777619u;return h?h:1;
}
void game_item_drag_reset(void){for(unsigned p=0;p<4;p++){drag_index[p]=0;drag_signature[p]=0;}}
int game_item_drag_index(unsigned owner){return owner<4?(int)drag_index[owner]-1:-1;}
uint32_t game_item_drag_hash(uint32_t h){for(unsigned p=0;p<4;p++){h=(h^drag_index[p])*16777619u;h=(h^drag_signature[p])*16777619u;}return h;}
unsigned game_item_drag_wire(unsigned code){return code<128?ITEM_MOVE_COMMAND|(code&3)|((code&124)<<2):0;}
static int drag_equip_ok(const Item *it,unsigned pos){
 unsigned slot=pos-BAG_CELLS;const ItemDef *d=&item_defs[it->kind];
 return slot<EQUIP_COUNT&&(slot==d->slot||(d->slot==EQUIP_RING1&&slot==EQUIP_RING2))&&
 game_effective_stat(0)>=item_strength(it)&&game_effective_stat(1)>=item_magic(it)&&game_effective_stat(2)>=d->dexterity;
}
static int drag_place(Game *g,unsigned index,unsigned dest){
 if(g->hp<=0)return INV_BUSY;if(index>=ITEM_SLOTS+10||dest>=BAG_CELLS+EQUIP_COUNT+10)return INV_REQUIREMENTS;
 Inventory staged=LIVE_INVENTORY;uint64_t belt=g->belt;
 unsigned from_belt=index>=ITEM_SLOTS;Item moving=from_belt?item_create(game_belt_kind(g,index-ITEM_SLOTS),1):staged.items[index];
 if(moving.kind>=ITEM_COUNT||(from_belt&&!game_belt_kind(g,index-ITEM_SLOTS)))return INV_BUSY;
 unsigned original=from_belt?BAG_CELLS+EQUIP_COUNT+index-ITEM_SLOTS:moving.pos;
 if(dest==original)return INV_OK;
 if(from_belt){index=ITEM_SLOTS;for(unsigned i=0;i<ITEM_SLOTS;i++)if(staged.items[i].kind==ITEM_EMPTY){index=i;break;}belt&=~((uint64_t)15<<((original-BAG_CELLS-EQUIP_COUNT)*4));}
 else staged.items[index].kind=ITEM_EMPTY;
 if(dest>=BAG_CELLS+EQUIP_COUNT){
  unsigned code=game_belt_code(moving.kind),slot=dest-BAG_CELLS-EQUIP_COUNT;if(!code)return INV_REQUIREMENTS;
  unsigned old=(unsigned)(belt>>(slot*4))&15;
  if(old){
   Item displaced=item_create(old<=3?old:old-4+ITEM_FULL_HEAL,1);
   if(from_belt)belt|=(uint64_t)old<<((original-BAG_CELLS-EQUIP_COUNT)*4);
   else{displaced.pos=original;staged.items[index]=displaced;if(!items_valid(&staged))return INV_FULL;}
  }
  belt=(belt&~((uint64_t)15<<(slot*4)))|((uint64_t)code<<(slot*4));
 }else{
  if(index>=ITEM_SLOTS)return INV_FULL;
  if(dest>=BAG_CELLS&&!drag_equip_ok(&moving,dest))return INV_REQUIREMENTS;
  unsigned displaced[2],n=0;
  if(dest<BAG_CELLS){
   unsigned x=dest%10,y=dest/10,w=item_width(&moving),h=item_height(&moving);uint8_t grid[BAG_CELLS];
   if(x+w>10||y+h>4||!items_grid(&staged,grid))return INV_FULL;
   for(unsigned dy=0;dy<h;dy++)for(unsigned dx=0;dx<w;dx++){unsigned j=grid[(y+dy)*10+x+dx];if(j==ITEM_EMPTY)continue;
    if(!n)displaced[n++]=j;else if(displaced[0]!=j)return INV_FULL;
   }
  }else for(unsigned j=0;j<ITEM_SLOTS;j++)if(staged.items[j].kind<ITEM_COUNT&&staged.items[j].pos>=BAG_CELLS){
   Item *old=&staged.items[j];
   if(old->pos==dest||((item_flags(&moving)&ITEM_TWOHAND)&&old->pos==BAG_CELLS+EQUIP_SHIELD)||(dest==BAG_CELLS+EQUIP_SHIELD&&(item_flags(old)&ITEM_TWOHAND))){if(n==2)return INV_FULL;displaced[n++]=j;}
  }
  Item old[2];for(unsigned j=0;j<n;j++){old[j]=staged.items[displaced[j]];staged.items[displaced[j]].kind=ITEM_EMPTY;}
  moving.pos=dest;staged.items[index]=moving;
  for(unsigned j=0;j<n;j++){
   if(from_belt){unsigned code=game_belt_code(old[j].kind);if(n!=1||!code)return INV_FULL;belt|=(uint64_t)code<<((original-BAG_CELLS-EQUIP_COUNT)*4);continue;}
   old[j].pos=original;staged.items[displaced[j]]=old[j];
   int fits=items_valid(&staged)&&(original<BAG_CELLS||drag_equip_ok(&old[j],original));
   if(!fits){staged.items[displaced[j]].pos=BAG_CELLS;if(!place(&staged,displaced[j]))return INV_FULL;}
  }
 }
 if(!items_valid(&staged))return INV_FULL;
 LIVE_INVENTORY=staged;g->belt=belt;g->potions=0;for(unsigned i=0;i<10;i++)g->potions+=!!game_belt_kind(g,i);
 game_spell_cancel(g);g->attack_wait=g->attack_duration=g->attack_queued=0;g->swing_target=-1;game_items_update(g);
 game_item_sound(g,item_sound_kind(&moving),1,g->x,g->y);return INV_OK;
}
int game_item_drag(Game *g,unsigned code){
 unsigned owner=drag_owner();
 if(code==127){drag_index[owner]=0;drag_signature[owner]=0;return INV_OK;}
 if(code<ITEM_SLOTS+10){uint32_t h=drag_key(g,code);if(!h||g->hp<=0)return INV_BUSY;drag_index[owner]=code+1;drag_signature[owner]=h;game_item_sound(g,ITEM_SOUND_GRAB,1,g->x,g->y);return INV_OK;}
 if(code<64||code>=64+BAG_CELLS+EQUIP_COUNT+10||!drag_index[owner])return INV_REQUIREMENTS;
 unsigned index=drag_index[owner]-1;
 if(drag_signature[owner]!=drag_key(g,index)){drag_index[owner]=0;drag_signature[owner]=0;return INV_BUSY;}
 int result=drag_place(g,index,code-64);if(result==INV_OK){drag_index[owner]=0;drag_signature[owner]=0;}return result;
}
