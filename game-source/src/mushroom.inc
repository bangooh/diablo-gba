/* Native Black Mushroom chain; advance only after a drop/exchange succeeds. */
static QUEST_FN int mushroom_drop(Game *g,unsigned kind,int x,int y,Drop *pending){
 int slot=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)return 0;
 for(int r=0;r<=3;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  int xx=x+dx,yy=y+dy;if(!game_item_space(g,xx,yy))continue;
  int tx=x,ty=y,ok=1;while(tx!=xx||ty!=yy){int sx=(xx>tx)-(xx<tx),sy=(yy>ty)-(yy<ty);if(!game_walkable(g,tx+sx,ty+sy)||(sx&&sy&&(!game_walkable(g,tx+sx,ty)||!game_walkable(g,tx,ty+sy)))){ok=0;break;}tx+=sx;ty+=sy;}if(!ok)continue;
  Item it=item_create(kind,item_seed(campaign_seed^0x4d555348u^kind));Drop d=item_to_drop(&it,xx,yy);game_drop_put(g,d);if(pending)*pending=d;game_item_sound(g,item_sound_kind(&it),0,xx,yy);return 1;
 }
 return 0;
}
static QUEST_FN void mushroom_spawn(Game *g){
 if(!game_campaign_layout()||g->area||dungeon_level!=9||!quest_progress.state[1])return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_MUSHROOM_PATCH)return;
 int slot=-1,x,y;for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){slot=i;break;}if(slot<0||!quest_place(g,0x4d555348u,1,&x,&y))return;
 /* Region generation is speculative until its Flash transaction commits. */
 if(!quest_progress.stage[1]&&!mushroom_drop(g,ITEM_FUNGAL_TOME,x+1,y,0))return;
 g->props[slot]=(Prop){x,y,PROP_MUSHROOM_PATCH,quest_progress.stage[1]>=3};
}
QUEST_FN int game_mushroom_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||dungeon_level!=9||index>=MAX_PROPS||quest_progress.state[1]!=2||quest_progress.stage[1]!=2)return 0;
 Prop *p=&g->props[index];if(p->kind!=PROP_MUSHROOM_PATCH||p->state)return 0;
 if(!mushroom_drop(g,ITEM_BLACK_MUSHROOM,p->x,p->y,0))return 0;p->state=1;quest_progress.stage[1]=3;return 1;
}
QUEST_FN int game_mushroom_killed(Game *g,const Enemy *e){
 if(!game_campaign_layout()||g->area||quest_progress.state[1]!=2||quest_progress.stage[1]!=4||e->kind==MONSTER_BUTCHER||e->kind==MONSTER_KING||e->kind==29||(e->kind>=MONSTER_LAZARUS&&e->kind<=MONSTER_DIABLO))return 0;
 if(mushroom_drop(g,ITEM_BRAIN,e->x,e->y,0))quest_progress.stage[1]=5;
 return 1; /* Full ground leaves the request pending for a later normal kill. */
}
static QUEST_FN int mushroom_item(unsigned kind){Inventory *bag=game_inventory();for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==kind)return i;return -1;}
static QUEST_FN int mushroom_talk(Game *g,unsigned npc){
 unsigned stage=quest_progress.stage[1];if(!quest_progress.state[1]||quest_progress.state[1]==3)return 0;
 int slot=-1;
 if(npc==3){
  if(stage<=1&&(slot=mushroom_item(ITEM_FUNGAL_TOME))>=0){game_inventory()->items[slot]=(Item){.kind=ITEM_EMPTY};quest_progress.state[1]=2;quest_progress.stage[1]=2;return 25;}
  if(stage==3&&(slot=mushroom_item(ITEM_BLACK_MUSHROOM))>=0){game_inventory()->items[slot]=(Item){.kind=ITEM_EMPTY};quest_progress.stage[1]=4;return 26;}
  if(stage==6&&mushroom_item(ITEM_SPECTRAL_ELIXIR)>=0){quest_progress.state[1]=3;quest_progress.stage[1]=7;return 28;}
  /* Hints once per interaction must not prevent ordinary shopping. */
 }else if(npc==6&&stage==5&&(slot=mushroom_item(ITEM_BRAIN))>=0){
  if(!mushroom_drop(g,ITEM_SPECTRAL_ELIXIR,55,80,&quest_progress.reward_mushroom))return 29;
  game_inventory()->items[slot]=(Item){.kind=ITEM_EMPTY};quest_progress.stage[1]=6;return 27;
 }
 return 0;
}
