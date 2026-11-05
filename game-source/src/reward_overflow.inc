/* Saved quest-stage bits: 0x80 reward owed, 0x40 warning dismissed.
 * No additional static RAM, and the reward's seed never depends on retry time.
 * Ground insertion order is preserved by game_drop_put and by floor saves. */
static QUEST_FN unsigned reward_floor(unsigned q){return q==2?4:q==3?8:q==4?14:q==6?2:q==11?13:REGION_KING;}
static QUEST_FN uint32_t reward_seed(unsigned q){return item_seed(campaign_seed^0x52574400u^q);}
QUEST_FN int game_reward_pending(const Game *g){
 if(!game_campaign_layout()||g->area||g->hp<=0)return -1;
 static const uint8_t ids[]={2,3,4,6,11,12};
 for(unsigned i=0;i<sizeof(ids);i++){unsigned q=ids[i];if((quest_progress.stage[q]&128)&&dungeon_level==reward_floor(q))return q;}
 return -1;
}
static QUEST_FN Item reward_item(unsigned q){
 if(q==2||q==3||q==4||q==11)return game_quest_loot(q,reward_seed(q));
 unsigned kind=q==6?ITEM_CLEAVER:q==12?ITEM_THE_UNDEAD_CROWN:item_candidate(item_seed(campaign_seed^0x67686172u),2*reward_floor(q));
 if(!kind)kind=ITEM_SHORT_SWORD;
 Item it=item_create_level(kind,reward_seed(q),2*reward_floor(q));
 if(q==6)item_make_unique(&it,0);else if(q==12)item_make_unique(&it,1);else item_generate_magic(&it,2*reward_floor(q),1);
 return it;
}
static QUEST_FN int reward_rank(const Drop *d){
 if(!d->active||d->kind>=ITEM_COUNT)return 4;
 Item it=item_from_drop(d);
 if(item_unique(&it))return 4;
 /* Guaranteed quest rewards are never recycled, including non-unique gear. */
 static const uint8_t ids[]={2,3,4,6,11,12};
 for(unsigned i=0;i<sizeof(ids);i++)if(d->seed==reward_seed(ids[i]))return 4;
 if(d->kind==ITEM_GOLD)return 0;
 if(game_belt_code(d->kind))return 1;
 if(!item_defs[d->kind].weight||item_defs[d->kind].slot>=EQUIP_COUNT)return 4;
 return (d->enchant&0x3fff)?3:2;
}
QUEST_FN int game_reward_candidate(const Game *g){
 int best=-1,rank=4;
 for(unsigned i=0;i<MAX_DROPS;i++){if(!g->drops[i].active)return -1;int r=reward_rank(&g->drops[i]);if(r<rank){rank=r;best=i;}}
 return best;
}
static QUEST_FN int reward_deliver(Game *g,unsigned q,int candidate){
 Drop previous={0};
 if(candidate>=0){previous=g->drops[candidate];g->drops[candidate]=(Drop){0};}
 int free_slot=0;for(unsigned i=0;i<MAX_DROPS;i++)free_slot+=!g->drops[i].active;
 if(!free_slot)goto failed;
 unsigned kind=q==2?MONSTER_GHARBAD:q==3?MONSTER_ZHAR:q==6?MONSTER_BUTCHER:q==11?MONSTER_WARLORD:MONSTER_KING;
 int ox=g->x,oy=g->y;
 if(q==4)for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_LACHDANAN){ox=g->props[i].x;oy=g->props[i].y;break;}
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].kind==kind&&!g->enemies[i].alive){ox=g->enemies[i].x;oy=g->enemies[i].y;break;}
 for(unsigned anchor=0;anchor<2;anchor++,ox=g->x,oy=g->y)for(int r=0;r<=3;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  int x=ox+dx,y=oy+dy;if(!game_item_space(g,x,y))continue;
  int tx=ox,ty=oy,ok=1;while(tx!=x||ty!=y){int sx=(x>tx)-(x<tx),sy=(y>ty)-(y<ty);
   if(!game_walkable(g,tx+sx,ty+sy)||(sx&&sy&&(!game_walkable(g,tx+sx,ty)||!game_walkable(g,tx,ty+sy)))){ok=0;break;}tx+=sx;ty+=sy;}
  if(!ok)continue;
  Item it=reward_item(q);if(game_drop_put(g,item_to_drop(&it,x,y))<0)goto failed;
  quest_progress.stage[q]=q==6?2:q==12?3:quest_progress.stage[q]&63;
  game_item_sound(g,item_sound_kind(&it),0,x,y);return 1;
 }
 failed:if(candidate>=0)g->drops[candidate]=previous;return 0;
}
QUEST_FN int game_reward_resolve(Game *g,int candidate,int accept){
 int q=game_reward_pending(g);if(q<0)return 0;
 if(accept&&candidate>=0&&candidate<MAX_DROPS&&candidate==game_reward_candidate(g)&&reward_rank(&g->drops[candidate])==3&&reward_deliver(g,q,candidate))return 1;
 quest_progress.stage[q]|=64;return 0;
}
QUEST_FN void game_reward_tick(Game *g){
 int q=game_reward_pending(g);if(q<0)return;
 int candidate=game_reward_candidate(g);
 if(candidate<0){if(reward_deliver(g,q,-1))return;}
 else if(reward_rank(&g->drops[candidate])<3){if(reward_deliver(g,q,candidate))return;}
 if(quest_progress.stage[q]&64)return;
 if(candidate>=0&&reward_rank(&g->drops[candidate])==3)g->town_message=42;
 else{g->town_message=43;quest_progress.stage[q]|=64;}
}
