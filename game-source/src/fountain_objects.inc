/* Retail OperateFountains/OperateSkelBook. Included after the shared reward
 * transaction and shrine stat helper; no new live or saved state. */
enum { ENV_NOTICE_BLOOD=187,ENV_NOTICE_PURIFYING,ENV_NOTICE_MURKY,
       ENV_NOTICE_TEARS,ENV_NOTICE_BOOKSTAND,ENV_NOTICE_RACK,ENV_NOTICE_DECAP };
static const char *fountain_name(unsigned kind){
 switch(kind){
 case PROP_BLOOD_FOUNTAIN:return "BLOOD FOUNTAIN";
 case PROP_PURIFYING_FOUNTAIN:return "PURIFYING SPRING";
 case PROP_MURKY_FOUNTAIN:return "MURKY POOL";
 case PROP_TEAR_FOUNTAIN:return "FOUNTAIN OF TEARS";
 case PROP_BOOKSTAND:return "BOOKSTAND";
 case PROP_SKEL_BOOK:return "SKELETON TOME";
 case PROP_THEME_DECAP:return "DECAPITATED BODY";
 case PROP_ARMOR_STAND:return "ARMOR STAND";
 case PROP_WEAPON_RACK:return "WEAPON RACK";
 case PROP_GOAT_SHRINE:return "GOAT SHRINE";
 case PROP_CAULDRON:return "CAULDRON";
 default:return 0;
 }
}
static const char *fountain_notice(unsigned notice){
 switch(notice){
 case ENV_NOTICE_BLOOD:return "ONE HEALTH RESTORED";
 case ENV_NOTICE_PURIFYING:return "ONE MANA RESTORED";
 case ENV_NOTICE_MURKY:return "INFRAVISION GRANTED";
 case ENV_NOTICE_TEARS:return "ONE STAT -1, ANOTHER +1";
 case ENV_NOTICE_BOOKSTAND:return 0;
 case ENV_NOTICE_RACK:return 0;
 case ENV_NOTICE_DECAP:return 0;
 default:return 0;
 }
}
/* Native RndTypeItems(ITYPE_MISC,IMISC_SCROLL): only ordinary scrolls,
 * iRnd != NEVER and minimum monster level <= 2*floor. Targeted SCROLLT
 * entries are deliberately excluded. Identify is its existing special item. */
static Item bookstand_reward(uint32_t seed){
 if(!random_n(&seed,5)){
  Item it=item_create(ITEM_BOOK_FIRE,seed);it.flags=(uint8_t)game_spell_book(seed,game_dungeon_depth());return it;
 }
 unsigned count=0,level=2*game_dungeon_depth();
 for(unsigned n=0;n<BASE_ITEM_COUNT;n++){
  const BaseItemDef *b=&base_item_defs[n];
  if(b->drop_rate&&b->type==0&&b->misc==21&&b->min_level<=level&&(b->spell==5||game_spell_supported(b->spell)))count++;
 }
 unsigned chosen=random_n(&seed,count?count:1);
 for(unsigned n=0;n<BASE_ITEM_COUNT;n++){
  const BaseItemDef *b=&base_item_defs[n];
  if(!b->drop_rate||b->type!=0||b->misc!=21||b->min_level>level||(b->spell!=5&&!game_spell_supported(b->spell)))continue;
  if(chosen--)continue;
  Item it=item_create(b->spell==5?ITEM_SCROLL_IDENTIFY:ITEM_BOOK_FIRE,seed);
  if(b->spell!=5)it.flags=(uint8_t)(128|b->spell);
  return it;
 }
 return item_create(ITEM_SCROLL_IDENTIFY,seed);
}
static Item theme_rack_reward(unsigned kind,uint32_t seed){
 unsigned depth=game_dungeon_depth(),type,good;
 if(kind==PROP_ARMOR_STAND){
  unsigned roll=random_n(&seed,2);type=depth<=5?6:depth<=9?8:9;good=depth<=5||depth>=13?1:depth<=9?roll:0;
 }else{type=1+random_n(&seed,4);good=depth>4;}
 unsigned count=0,level=2*depth;
 for(unsigned n=1;n<ITEM_COUNT;n++){const BaseItemDef *b=item_base(n);if(item_defs[n].weight&&b->drop_rate&&b->type==(int)type&&b->min_level<=level)count++;}
 unsigned chosen=random_n(&seed,count?count:1);
 for(unsigned n=1;n<ITEM_COUNT;n++){
  const BaseItemDef *b=item_base(n);if(!item_defs[n].weight||!b->drop_rate||b->type!=(int)type||b->min_level>level)continue;
  if(chosen--)continue;
  Item it=item_create_level(n,seed,level);item_generate_magic(&it,level,good);return it;
 }
 return item_create(ITEM_HEAL,seed); /* No eligible rack exists in valid floors. */
}
static int fountain_use(Game *g,unsigned index,uint32_t seed){
 Prop *p=&g->props[index];unsigned notice;
 switch(p->kind){
 case PROP_BLOOD_FOUNTAIN:
  game_hazard_sound(2);
  if(g->hp>=game_max_hp())return 1;
  if(++g->hp>=game_max_hp()){g->hp=game_max_hp();hero_progress.life_steal_fraction=0;}
  notice=ENV_NOTICE_BLOOD;break;
 case PROP_PURIFYING_FOUNTAIN:{
  game_hazard_sound(2);int mana=g->mana;unsigned debt=hero_progress.mana_debt;
  game_restore_mana(g,1,0);
  if(mana==g->mana&&debt==hero_progress.mana_debt)return 1;
  notice=ENV_NOTICE_PURIFYING;break;
 }
 case PROP_MURKY_FOUNTAIN:{
  unsigned family=(game_dungeon_depth()+3)/4;if(!family)family=1;if(family>4)family=4;
  unsigned ticks=1584;for(unsigned n=0;n<2*family;n++)ticks+=ticks>>3;
  hero_progress.infra_ticks=(uint16_t)ticks;p->state=1;game_hazard_sound(2);notice=ENV_NOTICE_MURKY;break;
 }
 case PROP_TEAR_FOUNTAIN:{
  unsigned subtract=random_n(&seed,4),add;
  do{add=random_n(&seed,4);}while(add==subtract);
  shrine_stat(g,subtract,-1);shrine_stat(g,add,1);p->state=1;game_hazard_sound(2);notice=ENV_NOTICE_TEARS;break;
 }
 case PROP_BOOKSTAND:case PROP_SKEL_BOOK:{
  Item reward=bookstand_reward(seed);
  if(!environment_rewards(g,index,&reward,1))return 0;
  p->state=1;notice=item_is_spell(&reward)&&!item_scroll(&reward)?GAME_BOOK_NOTICE_BASE+item_spell(&reward):ENV_NOTICE_BOOKSTAND;break;
 }
 case PROP_ARMOR_STAND:case PROP_WEAPON_RACK:{
  Item reward=theme_rack_reward(p->kind,seed);
  if(!environment_rewards(g,index,&reward,1))return 0;
  p->state=1;notice=ENV_NOTICE_RACK;break;
 }
 case PROP_THEME_DECAP:{
  unsigned kind=item_candidate(seed,2*game_dungeon_depth());Item reward=item_create(kind,seed);
  item_generate_magic(&reward,2*game_dungeon_depth(),0);
  if(!environment_rewards(g,index,&reward,1))return 0;
  p->state=1;notice=ENV_NOTICE_DECAP;break;
 }
 case PROP_GOAT_SHRINE:case PROP_CAULDRON:{
  unsigned type=game_shrine_type(g,index);if(!shrine_apply(g,index,type,seed))return 0;
  p->state=1;notice=ENV_NOTICE_FIRST+type;break;
 }
 default:return 0;
 }
 g->pickup_notice=(uint8_t)notice;g->pickup_notice_ticks=160;return 1;
}
/* Obj_BCrossDamage subtracts Q6 HP only on the cell north of the cross.
 * MI_Manashield observes that same loss, so apply its Q6 path before HP.
 * The existing positive HP fraction also holds fractional leech restoration;
 * subtracting from it preserves both effects without adding per-hero state. */
static void environment_cross_damage(Game *g){
 if(g->hp<=0)return;
 unsigned family=(game_dungeon_depth()+3)/4;if(!family)family=1;if(family>4)family=4;
 unsigned damage=4+2*family;damage-=damage*item_effects.resist[0]/100;
 damage=game_spell_absorb_q6(g,damage);
 unsigned health=(unsigned)g->hp*64+hero_progress.life_steal_fraction;
 health=damage>=health?0:health-damage;g->hp=(int16_t)(health/64);
 hero_progress.life_steal_fraction=g->hp?(uint8_t)(health%64):0;
}
static void environment_cross_tick(Game *g,const Prop *p){
 if(!coop_active){if(g->x==p->x&&g->y==(int)p->y-1)environment_cross_damage(g);return;}
 unsigned current=coop_turn,changed=0;
 for(unsigned n=0;n<4;n++)if((coop_members&(1u<<n))&&coop_players[n].hero.hp>0&&coop_players[n].x==p->x&&coop_players[n].y==(int)p->y-1){
  coop_select(g,n);environment_cross_damage(g);coop_capture(g,n);changed=1;
 }
 if(changed)coop_select(g,current);
}
