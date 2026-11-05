/* A control frame is separate from the10-bit movement/item channel. No command
 * can mutate a hero/world until every required token fragment is acknowledged. */
static unsigned coop_control_command;
static uint32_t coop_control_token,coop_reward_confirmation;
/* Bind a stat click to this hero's allocation state. A repeated acknowledged
 * command cannot spend twice; HP/mana fractions changing in combat do not
 * invalidate a click. No extra channel traffic is sent without a request. */
static UI_FN __attribute__((target("thumb"),minsize)) uint32_t coop_stat_token(unsigned id,unsigned stat){
 const CharacterState *h=&coop_players[id].hero;const HeroProgress *p=&h->progress;
 uint32_t token=2166136261u;unsigned values[]={coop_campaign_epoch,id,stat,h->reserved&255,p->level,p->points,p->strength,p->magic,p->dexterity,p->vitality};
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)token=(token^values[i])*16777619u;
 return token?token:1;
}
static UI_FN __attribute__((target("thumb"),minsize)) void coop_stat_request(unsigned stat){
 if(!coop_active||stat>=4||coop_control_command||shop_pending_command())return;
 const CharacterState *h=&coop_players[coop_id].hero;const HeroProgress *p=&h->progress;
 if(h->hp<=0||!p->points||(&p->strength)[stat]>=game_stat_limit(h->reserved&255,stat))return;
 coop_control_command=(14u<<6)+stat+1;coop_control_token=coop_stat_token(coop_id,stat);
}
static UI_FN __attribute__((minsize)) uint32_t coop_reward_token(unsigned action,unsigned slot){
 int q=game_reward_pending(&game);if(q<0||action<11||action>12)return 0;
 if(action==11&&(slot>=MAX_DROPS||(int)slot!=game_reward_candidate(&game)))return 0;
 uint32_t h=2166136261u;
 h=(h^action)*16777619u;h=(h^(unsigned)q)*16777619u;h=(h^quest_progress.stage[q])*16777619u;
 if(action==11){h=(h^slot)*16777619u;const uint8_t *b=(const uint8_t*)&game.drops[slot];for(unsigned i=0;i<sizeof(Drop);i++)h=(h^b[i])*16777619u;}
 return h?h:1;
}
static UI_FN __attribute__((target("thumb"),minsize)) int coop_exchange_commands(uint16_t held,uint16_t keys[4]){
#ifdef STAGE71_DEMO_INPUT
 /* Test build only. Production simulation/rendering, simulated input peers. */
 keys[0]=held;keys[1]=keys[2]=keys[3]=0;return 1;
#endif
 uint16_t commands[4]={0},part[4];uint32_t tokens[4]={0};
 unsigned local=coop_control_command?coop_control_command:shop_pending_command();
 uint32_t token=coop_control_command?coop_control_token:shop_pending_token();
 uint16_t hash=(uint16_t)coop_hash;
 unsigned control=0;
 int wire=coop_wire_input(held,keys,hash,local!=0,&control);
 if(!wire)return 0;
 if(wire>=16)return coop_roster_check((unsigned)wire&15)?3:0;
 if(control&&!coop_wire_frame((uint16_t)local,commands,hash))return 0;
 if(keys[coop_id]!=(held&4095)||commands[coop_id]!=local)return 0;
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&commands[i]){
  unsigned word=commands[i]-1,action=word>>6;
  if(action>15||(((action>=11&&action<=13)||action==15)&&(word&63))||(action==14&&(word&63)>=4))return 0;
 }
 unsigned any=0;for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i))any|=commands[i];
 if(any)for(unsigned b=0;b<3;b++){
  if(!coop_wire_frame((uint16_t)(token>>(12*b))&4095,part,hash))return 0;
  for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){
   if(b==2&&part[i]>255)return 0;
   tokens[i]|=(uint32_t)part[i]<<(12*b);
  }
 }
 if(any&&tokens[coop_id]!=token)return 0;
 unsigned leaving=0;
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&commands[i]){
  unsigned word=commands[i]-1,action=word>>6,slot=word&63;int result=SHOP_INVALID;
  coop_select(&game,i);
  if(action<=10)result=game_shop_command(&game,action,slot,tokens[i]);
  else if(action==15&&!slot&&tokens[i]==coop_campaign_epoch){leaving|=1u<<i;result=SHOP_OK;}
  else if(action==14&&tokens[i]==coop_stat_token(i,slot)){
   if(game_spend_stat(&game,slot))result=SHOP_OK;
  }
  else if(action==13&&!slot&&tokens[i]==coop_campaign_epoch){
   if(coop_restart_entrance(&game)){result=SHOP_OK;place(&coop_motion[i],game.x,game.y);}
  }
  else if(action<=12&&!slot){
   int candidate=game_reward_candidate(&game);
   if(tokens[i]&&tokens[i]==coop_reward_token(action,action==11?(unsigned)candidate:0))
    result=game_reward_resolve(&game,action==11?candidate:-1,action==11)?SHOP_OK:SHOP_INVALID;
  }
  coop_capture(&game,i);
  if(i==coop_id){
   if(coop_control_command){coop_control_command=0;coop_control_token=0;
    if(action==15){menu_mode=1;menu_choice=game.hp>0?1:3;}
    else if(action==14)quick_painted=0;
    else if(action==13){menu_mode=result==SHOP_OK?0:1;menu_choice=2;}
    else menu_mode=result==SHOP_OK?0:14;}
   else shop_acknowledge(result);
  }
 }
 coop_select(&game,coop_id);if(any)coop_checksum(&game);return leaving?(2|(leaving<<2)):1;
}
