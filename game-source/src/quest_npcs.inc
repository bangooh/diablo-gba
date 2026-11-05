/* Gharbad and Zhar use the original leave-sight / return conversation gates. */
static QUEST_FN unsigned npc_quest(unsigned kind){return kind==MONSTER_GHARBAD?2:kind==MONSTER_ZHAR?3:16;}
static QUEST_FN int npc_hostile(unsigned q){return quest_progress.state[q]==3||quest_progress.stage[q]>=(q==2?7u:3u);}
/* Leave-sight is a shared world gate. One distant party member must not
 * advance the conversation while another still sees the quest monster.
 * The visibility query reads position/terrain only, not selected hero stats. */
static QUEST_FN int npc_party_visible(Game *g,unsigned monster){
 if(!coop_active)return game_target_visible(g,monster);
 int x=g->x,y=g->y,visible=0;
 for(unsigned p=0;p<4&&!visible;p++)if((coop_members&(1u<<p))&&coop_players[p].hero.hp>0){
  g->x=coop_players[p].x;g->y=coop_players[p].y;visible=game_target_visible(g,monster);
 }
 g->x=x;g->y=y;return visible;
}
QUEST_FN void game_quest_npc_spawn(Game *g){
 coop_enemy_invalidate();
 unsigned q=dungeon_level==4?2:dungeon_level==8?3:16;
 if(!game_campaign_layout()||g->area||q==16||!quest_progress.state[q]||quest_progress.state[q]==3)return;
 int x,y;if(!quest_place(g,0x47484152u+q,1,&x,&y)&&!quest_place(g,0x47484152u+q,0,&x,&y))return;
 unsigned kind=q==2?MONSTER_GHARBAD:MONSTER_ZHAR;g->enemies[MAX_ENEMIES-2]=(Enemy){x,y,game_monster_hp(kind,campaign_seed),1,kind,0};
}
QUEST_FN void game_quest_npc_step(Game *g){
 if(!game_campaign_layout()||g->area)return;
 if(dungeon_level==13&&quest_progress.stage[11]==1){for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].kind==MONSTER_WARLORD&&npc_party_visible(g,i)){quest_progress.stage[11]=2;g->town_message=41;game_boss_intro(BOSS_INTRO_WARLORD);break;}}
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  Enemy *e=&g->enemies[i];
  if(e->kind==MONSTER_BUTCHER&&!(quest_progress.finale_flags&0x200)&&npc_party_visible(g,i))game_boss_intro(BOSS_INTRO_BUTCHER);
  unsigned q=npc_quest(e->kind);if(!e->alive||q==16||npc_hostile(q))continue;
  unsigned stage=quest_progress.stage[q];if((q==2?(stage==1||stage==3||stage==5):stage==1)&&!npc_party_visible(g,i))quest_progress.stage[q]++;
 }
}
static QUEST_FN int npc_reward(Game *g,const Enemy *e,unsigned q){
 unsigned kind=q==3?ITEM_BOOK_FIRE:item_candidate(item_seed(campaign_seed^0x67686172u),2*dungeon_level);
 if(q==2&&!kind)kind=ITEM_SHORT_SWORD;
 int slot=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)return 0;
 for(int r=1;r<=3;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  if(!game_item_space(g,e->x+dx,e->y+dy))continue;
  /* Both rewards stay in the reachable neighbourhood, including diagonals. */
  if(dx&&dy&&(!game_walkable(g,e->x+dx,e->y)||!game_walkable(g,e->x,e->y+dy)))continue;
  Item it=item_create_level(kind,item_seed(campaign_seed^0x71756573u^q^(quest_progress.stage[q]*0x34343u)),2*dungeon_level);
  if(q==2)item_generate_magic(&it,2*dungeon_level,1);
  if(q==3)it.flags&=127; /* Native guaranteed book, never a scroll. */
  game_drop_put(g,item_to_drop(&it,e->x+dx,e->y+dy));game_item_sound(g,item_sound_kind(&it),0,e->x+dx,e->y+dy);return 1;
 }
 return 0;
}
static QUEST_FN int npc_talk(Game *g){
 if(!game_campaign_layout()||g->area)return 0;
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  Enemy *e=&g->enemies[i];unsigned q=npc_quest(e->kind);if(!e->alive||q==16||npc_hostile(q)||!game_target_visible(g,i))continue;
  int dx=e->x-g->x,dy=e->y-g->y;if(dx<-1||dx>1||dy<-1||dy>1)continue;
  unsigned s=quest_progress.stage[q];
  if(q==2){
   if(s==0){quest_progress.state[q]=2;quest_progress.stage[q]=1;g->town_message=31;game_boss_intro(BOSS_INTRO_GHARBAD);}
   else if(s==2){if(!npc_reward(g,e,q)){g->town_message=37;return 1;}quest_progress.stage[q]=3;g->town_message=32;}
   else if(s==4){quest_progress.stage[q]=5;g->town_message=33;}
   else if(s==6){quest_progress.stage[q]=7;g->town_message=34;}
   else g->town_message=31;
  }else{
   if(s==0){if(!npc_reward(g,e,q)){g->town_message=37;return 1;}quest_progress.state[q]=2;quest_progress.stage[q]=1;g->town_message=35;game_boss_intro(BOSS_INTRO_ZHAR);}
   else if(s==2){quest_progress.stage[q]=3;g->town_message=36;}
   else g->town_message=35;
  }
  return 1;
 }
 return 0;
}
QUEST_FN int game_quest_npc_killed(Game *g,const Enemy *e){
 unsigned q=e->kind==MONSTER_WARLORD?11:npc_quest(e->kind);if(!game_campaign_layout()||q==16)return 0;
 if(quest_progress.state[q]==3)return 1;
 quest_progress.state[q]=3;quest_progress.stage[q]=128|(q==2?8:q==3?4:3);audio_quest_complete();
 game_reward_tick(g);return 1;
}

QUEST_FN void game_quest_bookcase(Game *g,const Prop *p){
  if(dungeon_level==8&&quest_progress.state[3]&&quest_progress.state[3]!=3&&quest_progress.stage[3]<3){
   for(unsigned n=0;n<MAX_ENEMIES;n++){
    const Enemy *e=&g->enemies[n];
    /* Native OperateBookCase requires a living, visible Zhar. Bind the
     * shelf to his library using the published transmission-room map,
     * rather than an arbitrary three-cell radius. No saved IDs needed. */
    int room=game_room_at(p->x,p->y);
    if(e->alive&&e->kind==MONSTER_ZHAR&&room>=0&&
       room==game_room_at(e->x,e->y)&&npc_party_visible(g,n)){
     quest_progress.state[3]=2;quest_progress.stage[3]=3;g->town_message=36;break;
    }
   }
  }
}
