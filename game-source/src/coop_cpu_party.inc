/* CPU-only session clocks, 20 simulation ticks per second, independent of draw FPS.
 * Preserve across travel; restart heal interval on death/revival. */
void __attribute__((minsize)) coop_cpu_reset(void){for(unsigned i=0;i<4;i++)cpu_dead_ticks[i]=cpu_heal_ticks[i]=cpu_far_ticks[i]=cpu_stuck_ticks[i]=0;}
static __attribute__((minsize)) void cpu_refill(Game *g){
 g->hp=game_max_hp();game_restore_mana(g,0,1);hero_progress.life_steal_fraction=0;
}
void __attribute__((minsize,noinline)) coop_cpu_step(Game *g,unsigned id){
 if(g->hp>0){cpu_dead_ticks[id]=0;if(++cpu_heal_ticks[id]>=6000){cpu_heal_ticks[id]=0;cpu_refill(g);}return;}
 cpu_heal_ticks[id]=0;
 if(cpu_dead_ticks[id]<1200&&++cpu_dead_ticks[id]<1200)return;
 const CoopPlayer *p=cpu_leader(g);if(!p)return;
 for(int r=1;r<=2;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  if(cpu_abs(dx)!=r&&cpu_abs(dy)!=r)continue;int x=p->x+dx,y=p->y+dy;
  if(!game_walkable(g,x,y)||coop_blocks(x,y,-1)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0)||!cpu_line(g,p->x,p->y,x,y))continue;
  game_spell_revive(g);g->x=x;g->y=y;g->attack_wait=g->move_wait=g->hit_flash=0;
  g->attack_queued=g->attack_duration=g->reaction_duration=g->reaction_block=0;
  g->target=g->swing_target=-1;coop_players[id].previous=0;cpu_dead_ticks[id]=0;cpu_refill(g);return;
 }
}
/* Base items are rolled only at party creation. Filter by level, requirements
 * and weapon class before equipping; never create illegal two-hand/shield kits. */
/* Party creation is cold; Thumb leaves room for the yielding movement code. */
#if defined(__arm__)
__attribute__((target("thumb")))
#endif
void __attribute__((minsize,noinline)) coop_cpu_equip(CharacterState *h,uint32_t seed){
 unsigned cls=h->reserved&255,level=h->progress.level;
 /* Spend earned points so level-matched CPUs can use level-appropriate gear. */
 static const uint8_t stats[3][5]={{0,3,0,3,2},{2,3,2,3,0},{1,1,3,1,0}};
 unsigned points=h->progress.points;uint8_t *values=&h->progress.strength;
 for(unsigned n=0;n<points;n++){
  unsigned stat=stats[cls][n%5],tries=0;while(values[stat]>=game_stat_limit(cls,stat)&&tries++<4)stat=(stat+1)&3;
  if(tries>4)break;values[stat]++;h->progress.points--;
  if(stat==1)h->progress.max_mana_q6+=cls==2?128:64;
  if(stat==3)h->progress.max_hp_q6+=cls==0?128:64;
 }
 for(unsigned j=0;j<ITEM_SLOTS;j++)h->inventory.items[j]=(Item){.kind=ITEM_EMPTY};
 unsigned twohand=0;
 static const uint8_t slots[]={EQUIP_HAND,EQUIP_HEAD,EQUIP_CHEST,EQUIP_RING1,EQUIP_RING2,EQUIP_NECK,EQUIP_SHIELD};
 for(unsigned j=0;j<EQUIP_COUNT;j++){
  unsigned slot=slots[j],count=0,chosen=ITEM_EMPTY;if(slot==EQUIP_SHIELD&&twohand)continue;
  for(unsigned k=0;k<ITEM_COUNT;k++){
   const ItemDef *d=&item_defs[k];if(!d->weight||d->slot!=(slot==EQUIP_RING2?EQUIP_RING1:slot)||d->level>level||d->strength>h->progress.strength||d->magic>h->progress.magic||d->dexterity>h->progress.dexterity)continue;
   Item probe={.kind=k};if(slot==EQUIP_HAND){if(cls==1?!(d->flags&ITEM_BOW):cls==2?!item_is_staff(&probe):((d->flags&ITEM_BOW)||item_is_staff(&probe)))continue;}
   seed=item_seed(seed+0x9e3779b9u);if(seed%++count==0)chosen=k;
  }
  if(chosen==ITEM_EMPTY)continue;
  Item it=item_create_level(chosen,seed,level);it.pos=BAG_CELLS+slot;item_set_durability(&it,item_max_durability(&it));
  if(cls==2&&slot==EQUIP_HAND){
   unsigned spell=0;
   for(unsigned attempt=0;attempt<16;attempt++){
    seed=item_seed(seed+1);spell=game_spell_staff(seed,level,1);
    if(spell==SPELL_FIREBOLT||spell==SPELL_LIGHTNING||spell==SPELL_FIREBALL||spell==SPELL_CHARGED||spell==SPELL_CHAIN||spell==SPELL_ELEMENTAL)break;
    spell=0;
   }
   if(!spell)spell=SPELL_FIREBOLT;
   it.flags=spell;it.charges=it.max_charges=spell_defs[spell].staff_min+seed%(spell_defs[spell].staff_max-spell_defs[spell].staff_min+1);
  }
  h->inventory.items[j]=it;if(slot==EQUIP_HAND)twohand=item_flags(&it)&ITEM_TWOHAND;
 }
 if(cls==2){h->progress.spell_slot[0]=h->progress.spell_selected=SPELL_STAFF;}
}
