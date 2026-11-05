#include "game.h"
#include "audio.h"
#include "coop.h"
#include "world.h"
#include "world_rooms.h"
#include "../generated/king_quest.h"
#include "../generated/water_quest.h"
#ifndef QUEST_FN
#define QUEST_FN __attribute__((section(".text.ui")))
#endif
QuestProgress quest_progress;
/* Presentation-only pending IDs. Shared quest state changes independently of
 * local sound settings; rendering consumes one cue when its channel is free.
 * A bitset preserves simultaneous co-op contacts without adding a FIFO. */
volatile uint32_t boss_audio_event;
QUEST_FN __attribute__((noinline)) void game_boss_intro(unsigned id){
 if(id>=BOSS_INTRO_COUNT)return;
 if(id<=BOSS_INTRO_KING){
  unsigned flag=1u<<(9+id);if(quest_progress.finale_flags&flag)return;
  quest_progress.finale_flags|=(uint16_t)flag;
 }
 boss_audio_event|=1u<<id;
}
uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
/* Native IDs/states: Q_ROCK=0, INIT=1, ACTIVE=2, DONE=3. Group 2
 * disables one of Blind/Rock/Blood. Blind and Blood have native room overlays. */
QUEST_FN void game_quest_init(QuestProgress *q,uint32_t seed){
 *q=(QuestProgress){0};q->state[14]=q->state[15]=q->state[5]=1;q->state[11]=item_seed(seed^0x51524734u)%2==0;q->state[12]=item_seed(seed^0x51524730u)%2;q->state[13]=!q->state[12];q->state[0]=item_seed(seed^0x51524732u)%3!=1;
 q->stage[0]=q->state[0]?0:2;q->state[10]=item_seed(seed^0x51524733u)%3!=2;
 q->state[9]=item_seed(seed^0x51524732u)%3!=2;
 q->state[2]=item_seed(seed^0x51524731u)%3!=2;q->state[3]=item_seed(seed^0x51524733u)%3!=1;
 q->state[1]=item_seed(seed^0x51524733u)%3!=0;
 q->state[7]=item_seed(seed^0x51524731u)%3!=1;
 q->state[8]=item_seed(seed^0x51524732u)%3!=0;q->state[4]=item_seed(seed^0x51524734u)%2!=0;
}
static QUEST_FN int reward_valid(const Drop *d,unsigned state,unsigned kind,unsigned tag){
 if(!d->active){const uint8_t *p=(const uint8_t*)d;for(unsigned i=0;i<sizeof(*d);i++)if(p[i])return 0;return 1;}
 Item it=item_from_drop(d);
 return state==3&&d->active==1&&d->kind==kind&&d->x<96&&d->y<96&&d->enchant==tag&&item_valid(&it);
}
QUEST_FN int game_quest_valid(const QuestProgress *q){
 if(!q||q->bone_gates>3||(q->finale_flags&~(BOSS_GEOMETRY_FLAGS|BOSS_SPEECH_FLAGS|TOWN_WARP_FLAGS))||q->banner_told>1||(!q->state[7]&&q->banner_told)||(q->stage[7]==1&&!q->banner_told)||(q->stage[7]==3&&!q->banner_told))return 0;
 if(q->king_told>2||q->king_gates>7||(!q->state[12]&&(q->king_told||q->king_gates))||(q->king_told==2&&q->state[12]!=3))return 0;
 for(unsigned i=0;i<16;i++){
  /* Diablo owns the full0..140 ending timer, not deferred-reward flag bits. */
  if(i==5){if(q->state[i]>3||q->stage[i]>140||(q->state[i]<3&&q->stage[i]))return 0;continue;}
  unsigned flags=q->stage[i]&192;
  if(flags&&((i!=2&&i!=3&&i!=4&&i!=6&&i!=11&&i!=12)||!(flags&128)||(i!=6&&q->state[i]!=3)))return 0;
  if(i==6){if(q->state[i]||(q->stage[i]&63)>2||((q->stage[i]&128)&&((q->stage[i]&63)!=1)))return 0;continue;}
  if(i==11||i==14){unsigned a=q->state[i],b=q->stage[i]&63,end=i==11?3:2;if(a>3||b>end||(a<2?b!=0:a==2?(b<1||b>=end):b!=end))return 0;continue;}
  if(i==2||i==3){unsigned a=q->state[i],b=q->stage[i]&63,end=i==2?8:4;if(a>3||b>end||(a<2?b!=0:a==2?(b<1||b>=end):b!=end))return 0;continue;}
  if(i==1){unsigned a=q->state[i],b=q->stage[i];if(a>3||b>8||(a==0?b!=0:a==1?b>1:a==2?(b<2||b>6):b<7))return 0;continue;}
  if(i==15){unsigned a=q->state[i],b=q->stage[i];if(a>3||b>8||(a<2?b>1:a==2?(b<2||b>6):b<7))return 0;continue;}
  if(i==12||i==13){unsigned state=q->state[i],stage=q->stage[i]&63;if(state>3||(state<2?stage!=0:state==2?stage!=1:stage<2||stage>3))return 0;continue;}
  if(i==7){unsigned state=q->state[i],stage=q->stage[i]&63;if(state>3||stage>4||(state<2?stage!=0:state==2?stage>2:stage<3))return 0;continue;}
  if(i==8){if(q->state[i]>3||q->stage[i]!=(q->state[i]>=2))return 0;continue;}
  if(i==4){unsigned s=q->stage[i]&63,state=q->state[i];if(state>3||s>3||(state<2?s!=0:state==2?(s<1||s>2):s!=3))return 0;continue;}
  if(i==9){unsigned s=q->stage[i],state=q->state[i];if(state>3||s>4||(state<2?s!=0:state==2?(s<1||s>4):s!=4))return 0;continue;}
  if(i!=0&&i!=10){if(q->state[i]||q->stage[i])return 0;continue;}
  if(q->state[i]>3||q->stage[i]!=(q->state[i]>1?q->state[i]-1:(!i&&!q->state[i]?2:0)))return 0;
 }
 if(q->reward_mushroom.active){const Drop *d=&q->reward_mushroom;Item it=item_from_drop(d);if(q->stage[1]!=6&&q->stage[1]!=7)return 0;if(d->active!=1||d->kind!=ITEM_SPECTRAL_ELIXIR||d->x>=96||d->y>=96||!item_valid(&it))return 0;}else{const uint8_t *p=(const uint8_t*)&q->reward_mushroom;for(unsigned i=0;i<sizeof(Drop);i++)if(p[i])return 0;}
 return reward_valid(&q->reward_water,q->stage[13],ITEM_RING_OF_TRUTH,0x4005)&&reward_valid(&q->reward_banner,q->state[7],ITEM_HARLEQUIN_CREST,0x4006)&&reward_valid(&q->reward,q->state[0],ITEM_EMPYREAN_BAND,0x4003)&&reward_valid(&q->reward_anvil,q->state[10],ITEM_GRISWOLDS_EDGE,0x4009);
}
QUEST_FN int game_quest_valor_room(int x,int y){
 if(game_campaign_layout()&&((dungeon_level==6&&quest_progress.state[14])||(dungeon_level==13&&quest_progress.state[11]))){int dx=x-quest_valor_cell%80,dy=y-quest_valor_cell/80;return dx>=-2&&dy>=-2&&dx<20&&dy<18;}
 if(game_campaign_layout()&&dungeon_level==2&&quest_progress.state[13]){int dx=x-quest_water_cell%80,dy=y-quest_water_cell/80;return dx>=-2&&dx<=13&&dy>=-2&&dy<=13;}
 if(game_campaign_layout()&&dungeon_level==3&&quest_progress.state[12]){int dx=x-quest_king_cell%80,dy=y-quest_king_cell/80;return dx>=-2&&dx<=16&&dy>=-2&&dy<=16;}
 if(game_campaign_layout()&&dungeon_level==4&&quest_progress.state[7]){int dx=x-quest_banner_cell%80,dy=y-quest_banner_cell/80;return dx>=-2&&dx<22&&dy>=-2&&dy<22;}
 if(game_campaign_layout()&&dungeon_level==7&&quest_progress.state[8]){int dx=x-quest_valor_cell%80,dy=y-quest_valor_cell/80;return dx>=-2&&dx<=27&&dy>=-2&&dy<=27;}
 if(!game_campaign_layout()||dungeon_level!=5||!quest_progress.state[9])return 0;
 int dx=x-quest_valor_cell%80,dy=y-quest_valor_cell/80;return dx>=-2&&dx<=23&&dy>=-2&&dy<=35;
}
static QUEST_FN int quest_place(const Game *g,unsigned salt,int radius,int *px,int *py){
 unsigned start=item_seed(campaign_seed^salt)%6400;
 for(unsigned n=0;n<6400;n++){
  unsigned c=(start+n)%6400;int x=c%80,y=c/80,ok=x>=3&&y>=3&&x<77&&y<77&&!game_quest_valor_room(x,y);if(!ok)continue;
  int dx=x-g->x,dy=y-g->y;if(dx>=-5&&dx<=5&&dy>=-5&&dy<=5)continue;
  dx=x-g->down_x;dy=y-g->down_y;if(dx>=-5&&dx<=5&&dy>=-5&&dy<=5)continue;
  for(int j=-radius;j<=radius&&ok;j++)for(int i=-radius;i<=radius;i++)if(!game_item_space(g,x+i,y+j)){ok=0;break;}
  if(ok){*px=x;*py=y;return 1;}
 }return 0;
}
QUEST_FN void game_king_killed(Game *g,const Enemy *e){
 if(!game_campaign_layout()||g->area||dungeon_level!=REGION_KING||e->kind!=MONSTER_KING||!quest_progress.state[12]||quest_progress.state[12]==3)return;
 quest_progress.state[12]=3;quest_progress.stage[12]=130;game_reward_tick(g);
}
QUEST_FN void game_king_population(Game *g){
 for(unsigned i=0;i<MAX_ENEMIES;i++){g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;}
 for(unsigned i=0;i<MAX_PROPS;i++)g->props[i]=(Prop){0};for(unsigned i=0;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};
 for(unsigned i=0;i<sizeof(king_guards)/sizeof(king_guards[0]);i++){const uint8_t *p=king_guards[i];g->enemies[i]=(Enemy){p[0],p[1],game_monster_hp(p[2],item_seed(campaign_seed^i)),2,p[2],0};}
 for(unsigned i=0;i<sizeof(king_objects)/sizeof(king_objects[0]);i++){const uint8_t *p=king_objects[i];unsigned state=p[2]==PROP_KING_LEVER?!!(quest_progress.king_gates&(p[1]==18?1:2)):p[2]>=PROP_KING_CRUX1?!!(quest_progress.king_gates&4):0;g->props[i]=(Prop){p[0],p[1],p[2],state};}
 unsigned hp=game_monster_hp(MONSTER_KING,campaign_seed);
 g->enemies[MAX_ENEMIES-1]=(Enemy){19,31,hp,quest_progress.state[12]==3?0:2,MONSTER_KING,hp};
}
QUEST_FN int game_king_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||g->hp<=0||index>=MAX_PROPS)return 0;Prop *p=&g->props[index];
 if(p->kind==PROP_KING_ENTRY&&dungeon_level==3&&quest_progress.state[12])return game_region_floor(g,REGION_KING);
 if(dungeon_level!=REGION_KING||p->state)return 0;
 if(p->kind==PROP_KING_LEVER){p->state=1;quest_progress.king_gates|=p->y==18?1:2;}
 else if(p->kind>=PROP_KING_CRUX1&&p->kind<=PROP_KING_CRUX3){
  p->state=1;unsigned intact=0;for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind>=PROP_KING_CRUX1&&g->props[i].kind<=PROP_KING_CRUX3&&!g->props[i].state)intact++;
  if(!intact)quest_progress.king_gates|=4;
 }else return 0;
 game_king_change(g,0,0);return 1;
}
static QUEST_FN void king_entry(Game *g){
 if(!game_campaign_layout()||dungeon_level!=3||!quest_progress.state[12])return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_KING_ENTRY)return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){g->props[i]=(Prop){quest_king_cell%80+12,quest_king_cell/80+7,PROP_KING_ENTRY,0};return;}
}
static QUEST_FN void extra_spawn(Game *g){
 if(!game_campaign_layout())return;
 unsigned kind=dungeon_level==7&&quest_progress.state[8]?PROP_BLIND_BOOK:dungeon_level==14&&quest_progress.state[4]?PROP_LACHDANAN:0;if(!kind)return;
 int slot=-1;for(unsigned i=0;i<MAX_PROPS;i++){if(g->props[i].kind==kind)return;if(slot<0&&!g->props[i].kind)slot=i;}if(slot<0)return;
 int x,y;if(!quest_place(g,kind==PROP_BLIND_BOOK?0x424c494eu:0x4c414348u,kind==PROP_BLIND_BOOK?2:1,&x,&y))return;
 g->props[slot]=(Prop){x,y,kind,kind==PROP_BLIND_BOOK?quest_progress.stage[8]:quest_progress.state[4]==3};
}
static QUEST_FN void banner_spawn(Game *g){
 if(!game_campaign_layout()||dungeon_level!=4||!quest_progress.state[7])return;
 unsigned slots[2],n=0;for(unsigned i=0;i<MAX_PROPS&&n<2;i++){if(g->props[i].kind==PROP_SNOTSPIL)return;if(!g->props[i].kind)slots[n++]=i;}if(n<2)return;
 int x=quest_banner_cell%80,y=quest_banner_cell/80;
 g->props[slots[0]]=(Prop){x+8,y+12,PROP_SNOTSPIL,0};g->props[slots[1]]=(Prop){x+10,y+3,PROP_SIGN_CHEST,quest_progress.stage[7]>=3};
}
static QUEST_FN void banner_hostile(Game *g){
 if(!game_campaign_layout()||g->area||dungeon_level!=4||!quest_progress.state[7]||quest_progress.stage[7]<3)return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_SNOTSPIL&&!g->props[i].state){
  Prop *p=&g->props[i];p->state=1;game_banner_change(g);game_monster_reset(MAX_ENEMIES-1);
  g->enemies[MAX_ENEMIES-1]=(Enemy){p->x,p->y,game_monster_hp(MONSTER_SNOTSPIL,campaign_seed),1,MONSTER_SNOTSPIL,0};coop_enemy_invalidate();return;
 }
}
QUEST_FN int game_banner_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||g->hp<=0||dungeon_level!=4||!quest_progress.state[7]||index>=MAX_PROPS)return 0;
 Prop *p=&g->props[index];unsigned stage=quest_progress.stage[7];if(p->state)return 0;
 if(p->kind==PROP_SNOTSPIL){
  if(stage<2){quest_progress.state[7]=2;quest_progress.stage[7]=2;game_banner_change(g);game_boss_intro(BOSS_INTRO_SNOTSPIL);return 1;}
  if(stage!=2)return 0;
  Inventory *bag=game_inventory();for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_TAVERN_SIGN){bag->items[i]=(Item){.kind=ITEM_EMPTY};quest_progress.state[7]=3;quest_progress.stage[7]=4;banner_hostile(g);return 1;}return 0;
 }
 if(p->kind!=PROP_SIGN_CHEST||stage!=2)return 0;
 int slot=-1,x=-1,y=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)goto full;
 for(int dy=-1;dy<=1&&x<0;dy++)for(int dx=-1;dx<=1;dx++)if(game_item_space(g,p->x+dx,p->y+dy)){x=p->x+dx;y=p->y+dy;break;}if(x<0)goto full;
 Item sign=item_create(ITEM_TAVERN_SIGN,item_seed(campaign_seed^0x5349474eu));game_drop_put(g,item_to_drop(&sign,x,y));p->state=1;game_item_sound(g,ITEM_TAVERN_SIGN,0,x,y);return 1;
 full:g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;
}
QUEST_FN void game_quest_enter(Game *g){
 /* Native Leoric speech is a level-entry cue, not a visibility taunt. The
  * first live tick follows a successful scene load and starts it only once. */
 if(game_campaign_layout()&&!g->area&&g->hp>0&&dungeon_level==REGION_KING&&quest_progress.state[12]==2)
  game_boss_intro(BOSS_INTRO_KING);
 if(game_campaign_layout()&&!g->area&&dungeon_level==REGION_WATER&&quest_progress.state[13]==2){
  unsigned alive=0;for(unsigned i=0;i<MAX_ENEMIES;i++)alive+=!!g->enemies[i].alive;
  if(!alive){quest_progress.state[13]=3;quest_progress.stage[13]=2;}
 }
 game_reward_tick(g);
 banner_hostile(g);
 if(!game_campaign_layout()||g->area||dungeon_level!=15||quest_progress.state[4]!=2||quest_progress.stage[4]!=1)return;
 int slot=-1,x,y;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}
 if(slot<0||!quest_place(g,0x474f4c44u,2,&x,&y))return;
 Item elixir=item_create(ITEM_GOLDEN_ELIXIR,item_seed(campaign_seed^0x454c4958u));game_drop_put(g,item_to_drop(&elixir,x,y));quest_progress.stage[4]=2;
}
QUEST_FN int game_quest_extra_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||g->hp<=0||index>=MAX_PROPS)return 0;
 Prop *p=&g->props[index];unsigned blind=p->kind==PROP_BLIND_BOOK,q=blind?8:4;
 if(blind&&p->state)return 0;
 if((blind?dungeon_level!=7:p->kind!=PROP_LACHDANAN||dungeon_level!=14)||!quest_progress.state[q]||quest_progress.state[q]==3)return 0;
 if(!blind&&!quest_progress.stage[4]){quest_progress.state[4]=2;quest_progress.stage[4]=1;return 1;}
 Inventory *bag=game_inventory();int source=-1,slot=-1,x,y;
 if(!blind){for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_GOLDEN_ELIXIR){source=i;break;}if(source<0)return 0;}
 for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)goto full;
 if(blind){x=quest_valor_cell%80+5;y=quest_valor_cell/80+5;for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].x==x&&g->drops[i].y==y)goto full;}
 else {x=y=-1;for(int radius=1;radius<=3&&x<0;radius++)for(int dy=-radius;dy<=radius&&x<0;dy++)for(int dx=-radius;dx<=radius;dx++)if(game_item_space(g,p->x+dx,p->y+dy)){x=p->x+dx;y=p->y+dy;break;}if(x<0)goto full;}
 Item reward=item_create(blind?ITEM_OPTIC_AMULET_0:ITEM_VEIL_OF_STEEL,item_seed(campaign_seed^(blind?0x4f505449u:0x5645494cu)));if(!item_make_unique(&reward,blind?3:6))return 0;
 if(source>=0)bag->items[source]=(Item){.kind=ITEM_EMPTY};quest_progress.state[q]=blind?2:3;quest_progress.stage[q]=blind?1:131;p->state=blind?1:2;game_drop_put(g,item_to_drop(&reward,x,y));
 game_item_sound(g,item_sound_kind(&reward),0,x,y);if(blind)game_world_valor_change(g,0,0);return 1;
 full:g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;
}
static QUEST_FN void valor_spawn(Game *g){
 if(!game_campaign_layout()||dungeon_level!=5||!quest_progress.state[9])return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_VALOR_BOOK)return;
 unsigned slots[2],count=0;for(unsigned i=0;i<MAX_PROPS&&count<2;i++)if(!g->props[i].kind)slots[count++]=i;if(count<2)return;
 int x=quest_valor_cell%80,y=quest_valor_cell/80;
 g->props[slots[0]]=(Prop){x+9,y+24,PROP_VALOR_BOOK,quest_progress.stage[9]!=0};
 g->props[slots[1]]=(Prop){x+9,y+16,PROP_VALOR_PEDESTAL,quest_progress.stage[9]==4};
}
QUEST_FN int game_quest_valor_use(Game *g,unsigned prop){
 if(!game_campaign_layout()||g->area||g->hp<=0||dungeon_level!=5||!quest_progress.state[9]||prop>=MAX_PROPS)return 0;
 Prop *p=&g->props[prop];unsigned stage=quest_progress.stage[9];int x=quest_valor_cell%80,y=quest_valor_cell/80;
 if(p->kind==PROP_VALOR_BOOK&&!stage){
  unsigned n=0;for(unsigned i=0;i<MAX_DROPS&&n<3;i++)if(!g->drops[i].active)n++;
  if(n<3){g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;}
  static const uint8_t offsets[3][2]={{3,10},{15,10},{9,17}};
  for(unsigned i=0;i<3;i++){Item stone=item_create(ITEM_BLOOD_STONE,item_seed(campaign_seed^0x424c4f44u^i));game_drop_put(g,item_to_drop(&stone,x+offsets[i][0],y+offsets[i][1]));}
  quest_progress.state[9]=2;quest_progress.stage[9]=1;p->state=1;return 1;
 }
 if(p->kind!=PROP_VALOR_PEDESTAL||!stage||stage>=4)return 0;
 Inventory *bag=game_inventory();int source=-1,slot=-1;
 for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_BLOOD_STONE){source=i;break;}if(source<0)return 0;
 Drop reward={0};
 if(stage==3){
  for(unsigned i=0;i<MAX_DROPS;i++){if(!g->drops[i].active){if(slot<0)slot=i;}else if(g->drops[i].x==x+9&&g->drops[i].y==y+3){slot=-1;break;}}
  if(slot<0){g->pickup_notice=5;g->pickup_notice_ticks=60;return 0;}
  Item armor=item_create(ITEM_ARKAINES_VALOR,item_seed(campaign_seed^0x56414c52u));if(!item_make_unique(&armor,7))return 0;reward=item_to_drop(&armor,x+9,y+3);
 }
 bag->items[source]=(Item){.kind=ITEM_EMPTY};quest_progress.stage[9]=stage+1;
 if(stage==3){p->state=1;game_drop_put(g,reward);game_item_sound(g,ITEM_ARKAINES_VALOR,0,x+9,y+3);}
 game_world_valor_change(g,0,0);return 1;
}
static QUEST_FN void water_entry(Game *g){
 if(!game_campaign_layout()||dungeon_level!=2||!quest_progress.state[13])return;
 unsigned slots[5],count=0;
 for(unsigned i=butcher_quest?13:0;i<MAX_PROPS;i++){
  if(g->props[i].kind==PROP_WATER_ENTRY)return;
  if(!g->props[i].kind&&count<5)slots[count++]=i;
 }
 if(count<5)return;
 int x=quest_water_cell%80+5,y=quest_water_cell/80+6;
 g->props[slots[0]]=(Prop){x,y,PROP_WATER_ENTRY,0};
 static const int8_t offsets[4][2]={{-2,1},{3,1},{-1,2},{2,2}};
 for(unsigned i=0;i<4;i++)g->props[slots[i+1]]=(Prop){x+offsets[i][0],y+offsets[i][1],PROP_WATER_CANDLE,0};
}

#include "mushroom.inc"
#include "quest_npcs.inc"
#include "late_quests.inc"
QUEST_FN void game_quest_spawn(Game *g){
 mushroom_spawn(g);late_spawn(g);
 game_finale_spawn(g);
 water_entry(g);
 king_entry(g);
 banner_spawn(g);
 valor_spawn(g);
 extra_spawn(g);
 if(game_campaign_layout()&&dungeon_level==10&&quest_progress.state[10]&&quest_progress.state[10]!=3&&quest_anvil_cell){
  for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].kind==ITEM_ANVIL_OF_FURY)return;
  int x=quest_anvil_cell%80,y=quest_anvil_cell/80;if(!game_walkable(g,x,y))return;
  for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){Item anvil=item_create(ITEM_ANVIL_OF_FURY,item_seed(campaign_seed^0x414e564cu));game_drop_put(g,item_to_drop(&anvil,x,y));return;}
 }
 if(!game_campaign_layout()||dungeon_level!=5||!quest_progress.state[0]||quest_progress.state[0]==3)return;
 int slot=-1,drop=-1;
 for(unsigned i=0;i<MAX_PROPS;i++){if(g->props[i].kind==PROP_ROCK_STAND)return;if(slot<0&&!g->props[i].kind)slot=i;}
 for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){drop=i;break;}
 if(slot<0||drop<0)return;
 unsigned start=item_seed(campaign_seed^0x524f434bu)%6400;
 for(unsigned n=0;n<6400;n++){
  unsigned c=(start+n)%6400;int x=c%80,y=c/80,ok=x>=2&&y>=2&&x<78&&y<78&&!game_quest_valor_room(x,y);if(!ok)continue;
  for(int dy=-2;dy<=2&&ok;dy++)for(int dx=-2;dx<=2;dx++)if(!game_walkable(g,x+dx,y+dy)){ok=0;break;}
  int dx=x-g->x,dy=y-g->y;if(dx>=-5&&dx<=5&&dy>=-5&&dy<=5)ok=0;
  dx=x-g->down_x;dy=y-g->down_y;if(dx>=-5&&dx<=5&&dy>=-5&&dy<=5)ok=0;
  if(!ok)continue;
  g->props[slot]=(Prop){x,y,PROP_ROCK_STAND,1};Item rock=item_create(ITEM_MAGIC_ROCK,item_seed(campaign_seed^0x524f434bu));game_drop_put(g,item_to_drop(&rock,x,y));return;
 }
}
QUEST_FN int game_quest_pickable(const Game *g,const Drop *d){
 if(g->area||d->kind!=ITEM_MAGIC_ROCK||d->x>=80||d->y>=80||!g->map[d->y][d->x])return 0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_ROCK_STAND&&g->props[i].x==d->x&&g->props[i].y==d->y)return 1;return 0;
}
QUEST_FN void game_quest_town(Game *g){
 if(!game_campaign_layout()||!g->area)return;
 for(unsigned n=0;n<5;n++){
  const Drop *d=n==4?&quest_progress.reward_mushroom:n==3?&quest_progress.reward_water:n==2?&quest_progress.reward_banner:n?&quest_progress.reward_anvil:&quest_progress.reward;if(!d->active)continue;int found=0;
  for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].kind==d->kind&&g->drops[i].seed==d->seed)found=1;
  if(!found)for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){game_drop_put(g,*d);break;}
 }
}
static QUEST_FN int quest_talk_one(Game *g,unsigned q,unsigned level,unsigned item,unsigned reward_kind,unsigned unique,Drop *pending){
 if(!(quest_progress.visited&(1u<<(level-1)))||!quest_progress.state[q]||quest_progress.state[q]==3)return 0;
 if(!quest_progress.stage[q]){
  if(q==10&&!(quest_progress.stage[0]==2||(quest_progress.state[0]==2&&quest_progress.stage[0]==1)))return 0;
  quest_progress.state[q]=2;quest_progress.stage[q]=1;return 1;
 }
 Inventory *bag=game_inventory();int source=-1,drop=-1,x=-1,y=-1;
 for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==item){source=i;break;}if(source<0)return 0;
 for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){drop=i;break;}if(drop<0)return 3;
 for(int r=0;r<=3&&x<0;r++)for(int dy=-r;dy<=r&&x<0;dy++)for(int dx=-r;dx<=r;dx++)if(game_item_space(g,(q==7?55:62)+dx,(q==7?63:64)+dy)){x=(q==7?55:62)+dx;y=(q==7?63:64)+dy;break;}
 if(x<0)return 3;
 Item reward=item_create(reward_kind,item_seed(campaign_seed^(q?0x47524953u:0x454d5059u)));if(!item_make_unique(&reward,unique))return 3;
 Drop staged=item_to_drop(&reward,x,y);bag->items[source]=(Item){.kind=ITEM_EMPTY};quest_progress.state[q]=3;quest_progress.stage[q]=q==7?3:2;*pending=staged;game_drop_put(g,staged);
 game_item_sound(g,item_sound_kind(&reward),0,x,y);return 2;
}
/* Native Pepin reward remains pending on the ground until picked up. */
static QUEST_FN int water_talk(Game *g){
 if(!(quest_progress.visited&1)||!quest_progress.state[13])return 0;
 if(quest_progress.state[13]==1){quest_progress.state[13]=2;quest_progress.stage[13]=1;return 18;}
 if(quest_progress.state[13]!=3||quest_progress.stage[13]!=2)return 0;
 int slot=-1,x=-1,y=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)return 20;
 for(int radius=0;radius<=3&&x<0;radius++)for(int dy=-radius;dy<=radius&&x<0;dy++)for(int dx=-radius;dx<=radius;dx++)if(game_item_space(g,55+dx,80+dy)){x=55+dx;y=80+dy;break;}
 if(x<0)return 20;
 Item ring=item_create(ITEM_RING_OF_TRUTH,item_seed(campaign_seed^0x57415452u));if(!item_make_unique(&ring,4))return 20;
 Drop d=item_to_drop(&ring,x,y);quest_progress.reward_water=d;game_drop_put(g,d);quest_progress.stage[13]=3;game_item_sound(g,item_sound_kind(&ring),0,x,y);return 19;
}
QUEST_FN void game_water_population(Game *g){
 for(unsigned i=0;i<MAX_ENEMIES;i++){g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;}
 for(unsigned i=0;i<MAX_PROPS;i++)g->props[i]=(Prop){0};for(unsigned i=0;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};
 for(unsigned i=0;i<sizeof(water_guards)/sizeof(water_guards[0]);i++){const uint8_t *p=water_guards[i];g->enemies[i]=(Enemy){p[0],p[1],game_monster_hp(p[2],item_seed(campaign_seed^i)),2,p[2],0};}
}
QUEST_FN int game_quest_talk(Game *g,unsigned npc){
 if(!game_campaign_layout()||!g->area||g->hp<=0)return 0;
 int mush=mushroom_talk(g,npc);if(mush)return mush;
 if(npc==7)return game_finale_talk(g);
 if(npc==6)return water_talk(g);
 if(npc==1){
  if((quest_progress.visited&4)&&quest_progress.state[7]&&quest_progress.state[7]!=3&&!quest_progress.banner_told){quest_progress.banner_told=1;if(quest_progress.state[7]==1){quest_progress.state[7]=2;quest_progress.stage[7]=1;}return 11;}
  int result=quest_talk_one(g,7,3,ITEM_TAVERN_SIGN,ITEM_HARLEQUIN_CREST,5,&quest_progress.reward_banner);if(result)return result+10;
  if(quest_progress.state[12]&&(quest_progress.visited&10)){
   if(!quest_progress.king_told){quest_progress.king_told=1;if(quest_progress.state[12]==1){quest_progress.state[12]=2;quest_progress.stage[12]=1;}return 16;}
   if(quest_progress.king_told==1&&quest_progress.state[12]==3){quest_progress.king_told=2;return 17;}
  }return 0;}
 if(npc!=0)return 0;
 int result=quest_talk_one(g,0,4,ITEM_MAGIC_ROCK,ITEM_EMPYREAN_BAND,2,&quest_progress.reward);if(result)return result;
 result=quest_talk_one(g,10,9,ITEM_ANVIL_OF_FURY,ITEM_GRISWOLDS_EDGE,8,&quest_progress.reward_anvil);return result?result+3:0;
}
QUEST_FN void game_quest_picked(Game *g,const Drop *d){
 if(!game_campaign_layout())return;
 /* Retail CheckQuestItem completes only after successful inventory insertion. */
 unsigned q=d->kind==ITEM_OPTIC_AMULET_0?8:d->kind==ITEM_ARKAINES_VALOR?9:16;
 if(q<16&&quest_progress.state[q]==2&&(q==8?quest_progress.stage[q]==1:quest_progress.stage[q]==4)){
  quest_progress.state[q]=3;audio_quest_complete();
 }
 if(d->kind==ITEM_FUNGAL_TOME&&quest_progress.state[1]==1&&!quest_progress.stage[1])quest_progress.stage[1]=1;
 if(!g->area)return;
 for(unsigned n=0;n<5;n++){Drop *p=n==4?&quest_progress.reward_mushroom:n==3?&quest_progress.reward_water:n==2?&quest_progress.reward_banner:n?&quest_progress.reward_anvil:&quest_progress.reward;if(p->active&&d->kind==p->kind&&d->seed==p->seed)*p=(Drop){0};}
}

QUEST_FN int game_quest_entry(Game *g){
 if(!game_campaign_layout()||g->area)return 0;
 if(npc_talk(g))return 1;
 if(dungeon_level==6){for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_BONE_ENTRY&&g->x==g->props[i].x&&g->y==g->props[i].y)return game_late_use(g,i);}
 if(dungeon_level==15||dungeon_level==REGION_VILE){for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==PROP_VILE_ENTRY&&g->x==g->props[i].x&&g->y==g->props[i].y)return game_finale_use(g,i);return 0;}
 if(dungeon_level!=2&&dungeon_level!=3)return 0;
 unsigned kind=dungeon_level==2?PROP_WATER_ENTRY:PROP_KING_ENTRY;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==kind){int dx=g->x-g->props[i].x,dy=g->y-g->props[i].y;if(dx>=-1&&dx<=1&&dy>=-1&&dy<=1){game_region_floor(g,kind==PROP_WATER_ENTRY?REGION_WATER:REGION_KING);return 1;}}
 return 0;
}

#include "finale.inc"

#include "reward_overflow.inc"
