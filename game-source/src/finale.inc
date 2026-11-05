/* Retail Betrayer / Diablo quest transitions, with GBA text-scene adapters. */
#include "../generated/finale_assets.h"
QUEST_FN int game_finale_protected(unsigned kind){if(kind==MONSTER_WARLORD)return quest_progress.stage[11]<2;if(kind==MONSTER_GHARBAD)return quest_progress.stage[2]<7;if(kind==MONSTER_ZHAR)return quest_progress.stage[3]<3;return dungeon_level==REGION_VILE&&quest_progress.stage[15]<6&&kind>=MONSTER_LAZARUS&&kind<=MONSTER_BLACK_JADE;}
static QUEST_FN void finale_guard(Game *g,unsigned slot,unsigned x,unsigned y,unsigned kind){
 /* A few native Hell guards occupy SOL-marked wall corners. Move only those
  * records to an adjacent free cell; retain all other native placements. */
 if(dungeon_level==16&&!game_walkable(g,x,y)){
  int done=0;for(int radius=1;radius<=2&&!done;radius++)for(int dy=-radius;dy<=radius&&!done;dy++)for(int dx=-radius;dx<=radius;dx++){
   if((dx<0?-dx:dx)+(dy<0?-dy:dy)!=radius||!game_walkable(g,(int)x+dx,(int)y+dy))continue;
   unsigned occupied=0;for(unsigned i=0;i<slot;i++)occupied|=g->enemies[i].x==(int)x+dx&&g->enemies[i].y==(int)y+dy;
   if(!occupied){x+=dx;y+=dy;done=1;break;}
  }
 }
 g->enemies[slot]=(Enemy){x,y,game_monster_hp(kind,item_seed(campaign_seed^slot)),2,kind,0};
}
QUEST_FN void game_finale_population(Game *g){
 for(unsigned i=0;i<MAX_ENEMIES;i++){g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;}
 for(unsigned i=0;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};for(unsigned i=0;i<MAX_PROPS;i++)g->props[i]=(Prop){0};
 unsigned n=0,p=0;
 if(dungeon_level==REGION_VILE){
  for(unsigned i=0;i<sizeof(finale_vile2_guards)/3;i++){const uint8_t *v=finale_vile2_guards[i];finale_guard(g,n++,v[0],v[1],v[2]);}
  for(unsigned i=0;i<sizeof(finale_vile2_objects)/3;i++){const uint8_t *v=finale_vile2_objects[i];g->props[p++]=(Prop){v[0],v[1],v[2],v[2]==PROP_VILE_BOOK?!!(quest_progress.finale_flags&(v[0]<20?1:2)):0};}
  finale_guard(g,n++,16,30,MONSTER_LAZARUS);finale_guard(g,n++,24,29,MONSTER_RED_VEX);finale_guard(g,n++,22,33,MONSTER_BLACK_JADE);
 }else if(dungeon_level==16){
  const uint8_t (*guards[4])[3]={finale_diab1_guards,finale_diab2a_guards,finale_diab3a_guards,finale_diab4a_guards};
  const unsigned counts[4]={sizeof(finale_diab1_guards)/3,sizeof(finale_diab2a_guards)/3,sizeof(finale_diab3a_guards)/3,sizeof(finale_diab4a_guards)/3};
  const uint8_t (*objects[3])[3]={finale_diab1_objects,finale_diab2a_objects,finale_diab3a_objects};
  const unsigned object_counts[3]={sizeof(finale_diab1_objects)/3,sizeof(finale_diab2a_objects)/3,sizeof(finale_diab3a_objects)/3};
  for(unsigned q=0;q<4;q++){
   unsigned x=2*(quest_diablo_cells[q]%40),y=2*(quest_diablo_cells[q]/40);
   for(unsigned i=0;i<counts[q];i++){const uint8_t *v=guards[q][i];finale_guard(g,n++,x+v[0],y+v[1],v[2]);}
   if(q<3)for(unsigned i=0;i<object_counts[q];i++){const uint8_t *v=objects[q][i];unsigned flag=q<2?16u<<q:128u<<i;g->props[p++]=(Prop){x+v[0],y+v[1],PROP_DIABLO_LEVER,!!(quest_progress.finale_flags&flag)};}
  }
  /* The native floor16 ordinary unique shares a loaded Blood Knight body.
   * Replace one fixed guard so the bounded floor never exceeds112 actors. */
  const uint8_t roster[3]={22,45,102};unsigned unique=game_unique_for_roster(16,roster,3);
  if(unique)for(unsigned i=0;i<n;i++)if(g->enemies[i].kind==MONSTER_BLOOD_KNIGHT){g->enemies[i].kind=unique;g->enemies[i].hp=game_monster_hp(unique,campaign_seed);g->enemies[i].max_hp=g->enemies[i].hp;break;}
 }
}
QUEST_FN void game_finale_spawn(Game *g){
 if(!game_campaign_layout()||g->area||dungeon_level!=15)return;
 unsigned slots[2],n=0;for(unsigned i=0;i<MAX_PROPS;i++){if(g->props[i].kind==PROP_LAZ_STAND)return;if(!g->props[i].kind&&n<2)slots[n++]=i;}if(n<2)return;
 int x,y;if(!quest_place(g,0x4c415a41u,1,&x,&y))return;
 g->props[slots[0]]=(Prop){x,y,PROP_LAZ_STAND,quest_progress.stage[15]!=0};
 /* The red portal is next to the16th-floor pentagram. It activates after Cain. */
 int px=-1,py=-1;for(int r=2;r<=5&&px<0;r++)for(int dy=-r;dy<=r&&px<0;dy++)for(int dx=-r;dx<=r;dx++)if(game_item_space(g,g->down_x+dx,g->down_y+dy)){px=g->down_x+dx;py=g->down_y+dy;break;}
 if(px<0){g->props[slots[0]]=(Prop){0};return;}
 g->props[slots[1]]=(Prop){px,py,PROP_VILE_ENTRY,0};
}
QUEST_FN int game_finale_talk(Game *g){
 if(quest_progress.state[15]==3&&quest_progress.stage[15]==7){quest_progress.stage[15]=8;return 22;}
 if(quest_progress.stage[15]>=2)return 0;
 Inventory *bag=game_inventory();for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_STAFF_OF_LAZARUS){bag->items[i]=(Item){.kind=ITEM_EMPTY};quest_progress.state[15]=2;quest_progress.stage[15]=2;return 21;}
 if(quest_progress.visited&(1u<<14)){(void)g;return 23;}return 0;
}
QUEST_FN int game_finale_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||g->hp<=0||index>=MAX_PROPS)return 0;Prop *p=&g->props[index];
 if(p->kind==PROP_VILE_ENTRY){if(dungeon_level==15&&quest_progress.stage[15]>=2)return game_region_floor(g,REGION_VILE);if(dungeon_level==REGION_VILE&&quest_progress.state[15]==3)return game_region_floor(g,15);return 0;}
 if(p->state)return 0;
 if(p->kind==PROP_LAZ_STAND&&dungeon_level==15&&!quest_progress.stage[15]){
  int slot=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)return 0;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(game_item_space(g,p->x+dx,p->y+dy)){Item staff=item_create(ITEM_STAFF_OF_LAZARUS,item_seed(campaign_seed^0x4c415a53u));game_drop_put(g,item_to_drop(&staff,p->x+dx,p->y+dy));p->state=1;quest_progress.stage[15]=1;return 1;}return 0;
 }
 if(p->kind==PROP_VILE_BOOK&&dungeon_level==REGION_VILE){
  unsigned left=p->x<20;quest_progress.finale_flags|=left?1:2;p->state=1;game_vile_change(g,0,0);g->x=left?11:27;g->y=13;return 1;
 }
 if(p->kind==PROP_DIABLO_LEVER&&dungeon_level==16){
  /* Native ordering: first, second, then the two grouped third-room levers. */
  unsigned order=0;for(unsigned i=0;i<index;i++)order+=g->props[i].kind==PROP_DIABLO_LEVER;
  unsigned flag=order<2?16u<<order:128u<<(order-2);p->state=1;quest_progress.finale_flags|=flag;
  if((quest_progress.finale_flags&384)==384)quest_progress.finale_flags|=64;
  game_diablo_change(g,0,0);return 1;
 }
 return 0;
}
QUEST_FN void game_finale_killed(Game *g,const Enemy *e){
 if(dungeon_level==REGION_VILE&&e->kind==MONSTER_LAZARUS&&quest_progress.state[15]!=3){quest_progress.state[15]=3;quest_progress.stage[15]=7;quest_progress.state[5]=2;audio_quest_complete();}
 if(dungeon_level==16&&e->kind==MONSTER_DIABLO&&quest_progress.state[5]!=3){
  audio_monster(MONSTER_DIABLO,2);
  quest_progress.state[5]=3;quest_progress.stage[5]=0;game_arrows_reset();g->attack_wait=g->move_wait=g->hit_flash=0;g->target=g->swing_target=-1;
  /* M_DiabloDeath stops all surviving monsters and begins the140-tick ending. */
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive){game_corpse_add(g,i);g->enemies[i].alive=0;g->enemy_attack[i]=0;}
 }
}
QUEST_FN int game_finale_step(Game *g){
 if(g->area)return 0;
 if(dungeon_level==16&&quest_progress.state[5]==3){
  /* Called once per world tick, including when the host hero has died. */
  if(quest_progress.stage[5]<140){
   quest_progress.stage[5]++;
   if(quest_progress.stage[5]==140){
    /* PrepDoEnding updates each peer's own hero regardless of death or kill
     * credit. Our shared simulation updates every participating hero once. */
    unsigned tier=game_difficulty+1;
    if(coop_active){
     for(unsigned p=0;p<4;p++)if(coop_members&(1u<<p)){
      HeroProgress *progress=p==coop_turn?&hero_progress:&coop_players[p].hero.progress;
      if(progress->completion<tier)progress->completion=tier;
      if(p==coop_turn)coop_players[p].hero.progress.completion=progress->completion;
     }
    }else if(hero_progress.completion<tier)hero_progress.completion=tier;
   }
  }
  game_corpses_step();
  if(quest_progress.stage[5]>=140)g->won=1;return 1;
 }
 if(dungeon_level!=REGION_VILE)return 0;
 if(quest_progress.state[15]==3){
  int found=0;for(unsigned i=0;i<MAX_PROPS;i++)found|=g->props[i].kind==PROP_VILE_ENTRY;
  if(!found)for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){g->props[i]=(Prop){19,16,PROP_VILE_ENTRY,0};break;}
 }else if(quest_progress.stage[15]==4){quest_progress.finale_flags|=8;quest_progress.stage[15]=6;game_vile_change(g,0,0);game_boss_intro(BOSS_INTRO_LAZARUS);
 }else if((quest_progress.finale_flags&3)==3&&!(quest_progress.finale_flags&4)){
  int actor=-1;
  if(coop_active){for(unsigned p=0;p<4;p++)if((coop_members&(1u<<p))&&coop_players[p].hero.hp>0&&coop_players[p].x==19&&coop_players[p].y==20){actor=(int)p;break;}}
  else if(g->x==19&&g->y==20)actor=0;
  if(actor>=0){
   quest_progress.finale_flags|=4;quest_progress.stage[15]=4;game_vile_change(g,0,0);
   if(coop_active){coop_players[actor].x=19;coop_players[actor].y=30;}
   if(!coop_active||(unsigned)actor==coop_turn){g->x=19;g->y=30;}
  }
 }
 return 0;
}
