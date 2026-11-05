/* Stage76: retail Slain Hero / StoryBook; restored, linked FlameHole.
 * Object IDs and all transient phases fit the existing 4-byte Prop/3-byte save.
 * Flame rows are contiguous after their lever; no per-frame link search. */
unsigned game_story_index(void){
 unsigned depth=dungeon_level==8?1:dungeon_level==12?2:0;
 return (item_seed(campaign_seed^0x53544f52u)%3)*3+depth;
}
static int remaining_use(Game *g,unsigned i){
 Prop *p=&g->props[i];
 if(p->kind==PROP_STORY_BOOK){p->state=1;g->town_message=44+game_story_index();return 1;}
 if(p->kind==PROP_FLAME_LEVER){
  p->state^=1;
  for(unsigned n=i+1;n<MAX_PROPS&&game_prop_is_flame(g->props[n].kind);n++){
   Prop *f=&g->props[n];
   if(p->state&&f->state&&f->state<11)f->state=11;
   /* Re-arming waits for a fresh proximity trigger after extinguishing. */
  }
  game_hazard_sound(0);return 1;
 }
 if(p->kind!=PROP_SLAIN_HERO||p->state||coop_active)return 0;
 uint32_t seed=environment_seed(g,i);unsigned cls=game_hero_class();
 Item reward=item_create(cls==0?ITEM_BREAST_PLATE:cls==1?ITEM_LONG_WAR_BOW:ITEM_BOOK_FIRE,seed);
 if(cls==2)reward.flags=SPELL_LIGHTNING;
 else {item_generate_magic(&reward,2*game_dungeon_depth(),1);}
 if(!environment_rewards(g,i,&reward,1))return 0;
 p->state=1;g->pickup_notice=cls==2?GAME_BOOK_NOTICE_BASE+SPELL_LIGHTNING:ENV_NOTICE_RACK;g->pickup_notice_ticks=160;return 1;
}
static int flame_near(const Prop *p,int x,int y){
 int dx=x-p->x,dy=y-p->y;
 return p->kind==PROP_FLAME_X?(dy==0&&dx>=-2&&dx<=2):(dx==0&&dy>=-2&&dy<=2);
}
static int flame_trigger(const Game *g,const Prop *p){
 if(!coop_active){if(g->hp>0&&flame_near(p,g->x,g->y))return 1;}
 else for(unsigned n=0;n<4;n++)if((coop_members&(1u<<n))&&coop_players[n].hero.hp>0&&flame_near(p,coop_players[n].x,coop_players[n].y))return 1;
 for(unsigned n=0;n<MAX_ENEMIES;n++)if(g->enemies[n].alive==1&&g->enemies[n].hp>0&&flame_near(p,g->enemies[n].x,g->enemies[n].y))return 1;
 return 0;
}
static void flame_step(Game *g,unsigned lever){
 unsigned end=lever+1;while(end<MAX_PROPS&&game_prop_is_flame(g->props[end].kind))end++;
 unsigned trigger=0;
 if(!g->props[lever].state)for(unsigned n=lever+1;n<end;n++)if(!g->props[n].state&&flame_trigger(g,&g->props[n])){trigger=1;break;}
 for(unsigned n=lever+1;n<end;n++){
  Prop *p=&g->props[n];unsigned state=p->state;
  if(!state&&trigger)p->state=2;
  else if(state>=2&&state<=10)p->state=state==10?1:state+1;
  else if(state>=11)p->state=state==15?0:state+1;
  /* Restoration adapter: original Obj_FlameTrap omitted damage entirely.
   * Existing fractional fire hazard handles armor-independent fire resistance,
   * mana shield and all co-op heroes without allocating a missile slot. */
  if(p->state==1&&!g->props[lever].state){Prop hit=*p;hit.y++;environment_cross_tick(g,&hit);}
 }
}
