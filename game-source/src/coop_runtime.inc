/* Host-campaign lifecycle and local presentation. Wire/control effects are
 * acknowledged before simulation; region I/O occurs only after an actor tick. */
#include "coop_campaign.h"
static unsigned coop_render_frame,coop_failed,coop_clock_valid,coop_clock_remainder;
static uint16_t coop_clock;
static UI_FN int coop_roster_check(unsigned mask);
#include "coop_commands.inc"
static UI_FN void coop_death_menu(void){
 /* Dialogue/panels borrow terrain and actor VRAM. Rebuild the frozen world
  * before the red death overlay; merely changing menus retains prose pixels. */
 dialogue_end();inventory_release();quick_mode=menu_mode=0;
 death_red_ready=0;draw(coop_render_frame);frame_commit();
 menu_mode=1;menu_choice=2;menu_draw();
}
static UI_FN void quest_redraw(void);
static UI_FN void valor_refresh(unsigned old_stage);
static UI_FN void banner_refresh(unsigned stage);
static UI_FN unsigned late_revision(void);
static UI_FN int late_refresh(unsigned old_level,unsigned old_revision);
static UI_FN void coop_pause(const char *message){
 if(link_diag.enabled&&link_diag.error!=LE_READY_TIMEOUT&&link_diag.error!=LE_TRANSFER_TIMEOUT&&link_diag.error!=LE_RECEIVE_TIMEOUT&&link_diag.error!=LE_DEPARTED)link_diag_halt();
 if(link_diag.wait_active){link_diag_wait_end();link_diag.wait_active=0;}
 dialogue_end();
 coop_failed=1;coop_wire_park();quick_mode=menu_mode=0;hud_text(2,17,message);
}
static UI_FN void coop_scene_resume(void){
 coop_clock_valid=coop_clock_remainder=0;
 loading_begin(loading_destination(game.area,dungeon_level));
 coop_campaign_prepare_scene();load_scene();reset_visuals();
 for(unsigned i=0;i<4;i++)place(&coop_motion[i],coop_players[i].x,coop_players[i].y);
 draw(coop_render_frame);reveal_scene();link_diag_stage(LD_RUNNING);
}
static UI_FN int coop_save_session(void){
 CharacterState hero=coop_players[coop_id].hero;
 if(hero.hp<=0)hero.hp=game_character_max_hp(&hero);
 return coop_id?coop_campaign_personal_save(&hero):coop_campaign_host_save();
}
static UI_FN void coop_leave_finish(unsigned saved,unsigned all_saved);
static UI_FN __attribute__((minsize)) void coop_leave(unsigned coordinated){
 dialogue_end();
 inventory_release();audio_stop();quick_mode=menu_mode=0;frame_state&=~1u;
 link_diag_end_screen(2,1);
 if(coordinated){
  link_diag_stage(LD_LEAVE_READY);
  if(!campaign_vote(1,0))link_diag_halt();
 }
 link_diag_stage(LD_LEAVE_WRITE);unsigned saved=coop_save_session(),all_saved=1;
 if(coordinated){
  uint16_t values[4];link_diag_stage(LD_LEAVE_ACK);
  if(!coop_wire_commit_frame((uint16_t)saved,values,0))link_diag_halt();
  /* Keep the other peers' result separate, so a successful local retry does
   * not incorrectly report that another player's save failed. */
  for(unsigned i=0;i<4;i++)if(i!=coop_id&&((coop_members&~coop_cpu_mask)&(1u<<i)))all_saved&=values[i]==1;
  link_diag_stage(LD_LEAVE_LINK);
  if(!coop_wire_finish())link_diag_halt();
 }
 if(coordinated)coop_wire_end();else coop_wire_park();link_state.active=0;link_diag_stage(LD_LEAVE_DONE);
 coop_leave_finish(saved,all_saved);
}
static UI_FN __attribute__((minsize)) void coop_leave_finish(unsigned saved,unsigned all_saved){
 /* A failed local write keeps the live hero/world available for a retry.
  * Other peers may return to title after their own verified save. */
 while(!saved){
  link_diag_end_screen(saved,all_saved);
  unsigned previous=(~R16(0x04000130))&1023;
  for(;;){
   while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
   unsigned held=(~R16(0x04000130))&1023,pressed=held&~previous;previous=held;
   if(pressed&KEY_A)break;
  }
  link_diag_end_screen(2,1);saved=coop_save_session();
 }
 shop_cancel_pending();coop_control_command=0;coop_region_clear();
 /* Read the private difficulty before the title borrows archive scratch. */
 unsigned difficulty=save_buffer.metadata_padding2;
 coop_cpu_mask=0;coop_active=coop_campaign_active=0;active_game=0;menu_mode=5;
 game_set_difficulty(difficulty);
}
/* Mask changes occur only between acknowledged game inputs. Include full
 * world hash and simulation tick, not just the ordinary 12-bit wire hash. */
static UI_FN int coop_roster_check(unsigned mask){
 unsigned members=mask|coop_cpu_mask;
 if(!(mask&1)||!(mask&(1u<<coop_id))||(members&~coop_members))return link_diag_fault(LE_TOPOLOGY,coop_members,members);
 coop_set_members(&game,members);
 if(!campaign_agree(coop_hash)||!campaign_agree(coop_ticks))return 0;
 coop_clock_valid=coop_clock_remainder=0;link_state.members=mask;
 return 1;
}
static UI_FN void coop_depart(unsigned leaving){
 dialogue_end();inventory_release();quick_mode=0;
 unsigned local=!!(leaving&(1u<<coop_id));
 if(local){frame_state&=~1u;link_diag_end_screen(2,1);}
 link_diag_stage(LD_LEAVE_READY);if(!campaign_vote(1,0)){coop_pause("LINK LOST A SAVE EXIT");return;}
 link_diag_stage(LD_LEAVE_WRITE);unsigned saved=local?coop_save_session():1;
 uint16_t values[4];link_diag_stage(LD_LEAVE_ACK);
 if(!coop_wire_commit_frame((uint16_t)saved,values,0)){coop_pause("LINK LOST A SAVE EXIT");return;}
 unsigned removed=leaving&~coop_wire_members();
 for(unsigned i=1;i<4;i++)if((leaving&(1u<<i))&&values[i]==1)removed|=1u<<i;
 link_diag_stage(LD_LEAVE_LINK);
 if(removed&&!coop_wire_remove(removed)){coop_pause("LINK LOST A SAVE EXIT");return;}
 if(local&&saved){
  audio_stop();quick_mode=menu_mode=0;frame_state&=~1u;
  coop_wire_park();link_state.active=0;link_diag_stage(LD_LEAVE_DONE);coop_leave_finish(1,1);return;
 }
 if(local&&!saved){coop_scene_resume();menu_mode=1;menu_choice=1;hud_text(2,17,"SAVE FAILED - TRY AGAIN");}
 link_diag_stage(LD_RUNNING);coop_clock_valid=coop_clock_remainder=0;
}
static UI_FN __attribute__((minsize)) void coop_enter(void){
 /* The BGM buffer becomes the exact equipment cache during this session. */
 audio_stop();
 link_diag_start(link_state.role,link_state.members);link_diag_stage(LD_SAVE);
 coop_id=link_state.role;coop_members=link_state.members|coop_cpu_mask;coop_failed=0;coop_render_frame=0;coop_clock_valid=0;coop_clock_remainder=0;
 uint32_t previous_history[MONSTER_HISTORY_TYPES];for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)previous_history[i]=monster_history[i];
 CharacterState local;
 if(save_read()){local=save_buffer.hero;for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=saved_history(i);}
 else{link_diag_stage(LD_EMPTY);if(!coop_campaign_can_initialize()){link_diag_fault(LE_SAVE_INVALID,1,0);goto failed;}game_create_hero(&local,0);for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=0;}
 R16(0x04000100)=0;R16(0x04000102)=0x0083;
 link_diag_stage(LD_SYNC);
 if(!coop_wire_begin(coop_id,link_state.members))goto failed;
 int result=coop_campaign_enter(&local);
 if(!result)goto failed;
 quick_mode=quick_blocked=inventory_command=0;coop_control_command=0;shop_cancel_pending();coop_region_clear();
 map_visible=menu_mode=0;active_game=session_hero=1;link_diag_stage(LD_SCENE);link_diag.screen=0;coop_scene_resume();
 if(result==COOP_CAMPAIGN_COMMITTED_LOST)coop_pause("ENTRY SAVED LINK LOST");
 link_diag_stage(LD_RUNNING);coop_wire_recovery(1);
 return;
 failed:for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=previous_history[i];link_diag_halt();
}
/* Town interaction uses the acknowledged actor, even on another peer's screen.
 * Only menu presentation is local; turn-in, healing and quest effects are shared. */
/* NPC/shop interaction is a cold ROM path; keep tile compositing in IWRAM. */
UI_FN void game_coop_actor_interaction(Game *g,unsigned pressed){
 if((coop_cpu_mask&(1u<<coop_turn))||!coop_campaign_active||g->hp<=0||g->won)return;
 if(g->area&&!g->paused&&!g->move_wait&&g->target!=TARGET_ITEM&&(pressed&KEY_A)){
  int npc=nearby_npc();if(npc>=0){
   g->town_message=(uint8_t)(npc+1);g->pickup_notice_ticks=60;
   if(npc>=9){if(coop_turn==coop_id)audio_cow(game_hero_class());return;}
   unsigned wounded_first=npc==2&&butcher_quest==1;
   int q=game_quest_talk(g,npc);
   if(npc==6){g->hp=game_max_hp();hero_progress.life_steal_fraction=0;}
   if(npc==2&&butcher_quest==1){butcher_quest=2;g->pickup_notice_ticks=160;}
   if(coop_turn==coop_id)dialogue_open((unsigned)npc,(unsigned)q,wounded_first);
  }
 }
 if(!g->area&&g->town_message>=31&&g->town_message<=52){
  unsigned notice=g->town_message;g->town_message=0;
  if(coop_turn==coop_id&&!menu_mode&&!quick_mode){
   if(notice==42){confirm_action=game_reward_candidate(g);confirm_yes=1;coop_reward_confirmation=coop_reward_token(11,confirm_action);menu_mode=13;}
   else if(notice==43)menu_mode=14;
   else{quest_notice=notice;menu_mode=11;}
  }
 }
}
typedef struct {unsigned level,valor,veil,banner,king,water,finale,laz,late;} CoopVisualState;
static UI_FN CoopVisualState coop_visual_capture(void){
 return (CoopVisualState){dungeon_level,quest_progress.stage[dungeon_level==7?8:9],quest_progress.stage[4]&63,quest_progress.stage[7],quest_progress.king_gates,quest_progress.state[13],quest_progress.finale_flags,quest_progress.stage[15],late_revision()};
}
static UI_FN void coop_visual_refresh(const CoopVisualState *v){
 if(game.area)return;
 (void)late_refresh(v->level,v->late);water_refresh(v->water);
 if((dungeon_level==5||dungeon_level==7)&&v->valor!=quest_progress.stage[dungeon_level==7?8:9])valor_refresh(v->valor);
 if((v->finale^quest_progress.finale_flags)&BOSS_GEOMETRY_FLAGS){
  if(dungeon_level==REGION_VILE)game_vile_change(0,town_map,(uint8_t*)town_map+12800);
  else if(dungeon_level==16)game_diablo_change(0,town_map,(uint8_t*)town_map+12800);
  quest_redraw();
 }
 if(dungeon_level==REGION_KING&&v->king!=quest_progress.king_gates){game_king_change(0,town_map,(uint8_t*)town_map+12800);quest_redraw();}
 if(dungeon_level==4&&v->banner!=quest_progress.stage[7])banner_refresh(quest_progress.stage[7]);
 if(dungeon_level==14&&v->veil!=(quest_progress.stage[4]&63)&&!menu_mode){quest_notice=(quest_progress.stage[4]&63)==3?10:9;menu_mode=11;}
 if(dungeon_level==REGION_VILE&&v->laz!=6&&quest_progress.stage[15]==6&&!menu_mode){quest_notice=24;menu_mode=11;}
}
/* Same pre-impact cue as solo PM_DoAttack presentation. Run once for each
 * advanced simulation tick, not once per rendered/wire frame. Coalesce nearby
 * simultaneous swings because they share one sample; prefer the local hero. */
static UI_FN void coop_melee_audio(void){
 if(game.area)return;
 for(unsigned n=0;n<4;n++){
  unsigned i=(coop_id+n)&3;const CoopPlayer *p=&coop_players[i];
  if(!(coop_members&(1u<<i))||p->hero.hp<=0||!p->attack_wait||SPELL_CASTING(p->swing)||game_attack_bow(p->attack_duration))continue;
  int dx=p->x-game.x,dy=p->y-game.y;if(dx<-6||dx>6||dy<-6||dy>6)continue;
  if(p->attack_wait==game_attack_hit_wait(p->hero.reserved&255,p->attack_duration)+1){audio_effect(0,1);return;}
 }
}
static UI_FN void coop_run_frame(unsigned held,unsigned pressed){
 /* Reconnect timeout has already expired before coop_failed is set. */
 if(coop_failed){coop_leave(0);return;}
 if(game.won)return; /* Common ending presentation is entered by main. */
 if(game.hp<=0&&(!menu_mode||menu_mode>=7))coop_death_menu();
 unsigned modal=menu_mode!=0;
 if(modal){
  quick_mode=0;quick_blocked&=held;quick_blocked|=held;
  if(game.hp>0&&menu_mode==1){
   if(pressed&(KEY_UP|KEY_DOWN))menu_choice^=1;
   if(pressed&(KEY_B|KEY_START))menu_close();
   else if(pressed&KEY_A){
    if(menu_choice){coop_control_command=(15u<<6)+1;coop_control_token=coop_campaign_epoch;}
    else{menu_mode=2;menu_choice=1;}
   }
  }else if(game.hp<=0&&menu_mode==1){
   if(!coop_control_command){
    if(pressed&KEY_UP)menu_choice=menu_choice<=1?3:menu_choice-1;
    if(pressed&KEY_DOWN)menu_choice=menu_choice>=3?1:menu_choice+1;
    if(pressed&KEY_A){
     if(menu_choice==1){menu_mode=2;menu_choice=1;}
     else if(menu_choice==2){coop_control_command=(13u<<6)+1;coop_control_token=coop_campaign_epoch;}
     else if(menu_choice==3){coop_control_command=(15u<<6)+1;coop_control_token=coop_campaign_epoch;}
    }
   }
  }else if(menu_mode==13){
   if(!coop_control_command){
    if(pressed&(KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN))confirm_yes^=1;
    if(pressed&(KEY_A|KEY_B|KEY_START)){
     unsigned action=(pressed&KEY_A)&&confirm_yes?11:12;
     coop_control_command=(action<<6)+1;
     coop_control_token=action==11?coop_reward_confirmation:coop_reward_token(12,0);
    }
   }
  }else menu_input(pressed,&coop_render_frame);
  held=pressed=0;
 }

 unsigned popup_input=modal?1:quick_input(held,pressed);
 if(popup_input)held=0;else held&=~quick_blocked;
 held&=~(KEY_START|KEY_SELECT);
 if(inventory_command)held=inventory_command;
 else held=options_dpad_keys(held,dpad_rotation);
 uint16_t keys[4]={0};unsigned steps=1;uint16_t now=audio_game_clock();
 if(!coop_id){
  if(coop_clock_valid){
   coop_clock_remainder+=(uint16_t)(now-coop_clock);
   steps=coop_clock_remainder/819u;if(steps>3)steps=3;
   coop_clock_remainder-=steps*819u;
   if(coop_clock_remainder>819u*6)coop_clock_remainder=819u*6;
  }
  coop_clock=now;coop_clock_valid=1;
  held=(held&~(KEY_SELECT|KEY_START))|(steps<<2);
 }
 unsigned control_menu=menu_mode,was_dead=game.hp<=0,recovered=link_diag.wait_recovered;
 unsigned profile_link=audio_game_clock();
 int exchange=coop_exchange_commands((uint16_t)held,keys);
 profile_time(2,profile_link);
 unsigned profile_sim=audio_game_clock();
 if(exchange==3){coop_clock_valid=coop_clock_remainder=0;draw(coop_render_frame);frame_commit();hud_text(2,17,"PARTY UPDATED");return;}
 if(!exchange){
  coop_pause("LINK PAUSED A SAVE EXIT");return;
 }
 /* Apply the acknowledged input once, then restart the pacing clock.
  * A cable wait must not turn into several accelerated catch-up ticks. */
 if(recovered!=link_diag.wait_recovered)coop_clock_valid=coop_clock_remainder=0;
 if(control_menu==13&&!menu_mode)menu_clear();
 inventory_command=0;
 /* Apply once per acknowledged wire frame, even if host simulation steps=0.
  * Every peer uses host-broadcast player order; an item transfer is atomic. */
 unsigned inventory_changed=0;
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&((keys[i]&ITEM_MOVE_MASK)==ITEM_MOVE_COMMAND)){
  inventory_changed=1;coop_select(&game,i);unsigned code=(keys[i]&3)|((keys[i]>>2)&124);
  int result=game_item_drag(&game,code);coop_capture(&game,i);if(i==coop_id)inventory_notice=result;keys[i]&=KEY_START|KEY_SELECT;
 }

 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&((keys[i]&GOLD_COMMAND_MASK)==GOLD_COMMAND)){
  inventory_changed=1;coop_select(&game,i);int result=game_gold_drop(&game,100*ITEM_COMMAND_INDEX(keys[i]));coop_capture(&game,i);if(i==coop_id)inventory_notice=result;keys[i]&=KEY_START|KEY_SELECT;
 }
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&((keys[i]&SPELL_COMMAND)==SPELL_COMMAND)){
  inventory_changed=1;coop_select(&game,i);game_spell_command(&game,keys[i]);
  coop_capture(&game,i);keys[i]&=KEY_START|KEY_SELECT;
 }
 for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&(keys[i]&ITEM_COMMAND)){
  inventory_changed=1;
  coop_select(&game,i);int result=((keys[i]&(ITEM_IDENTIFY_COMMAND|ITEM_DROP_COMMAND))==(ITEM_IDENTIFY_COMMAND|ITEM_DROP_COMMAND))?game_skill_item(&game,ITEM_COMMAND_INDEX(keys[i])):(keys[i]&ITEM_IDENTIFY_COMMAND)?game_identify_item(&game,ITEM_COMMAND_INDEX(keys[i])):game_item_action(&game,ITEM_COMMAND_INDEX(keys[i]),!!(keys[i]&ITEM_DROP_COMMAND));
  coop_capture(&game,i);if(i==coop_id)inventory_notice=result;
  keys[i]&=KEY_START|KEY_SELECT;
 }
 if(inventory_changed){coop_select(&game,coop_id);coop_checksum(&game);}
 if(exchange&2){if((unsigned)exchange>>2&1)coop_leave(1);else coop_depart((unsigned)exchange>>2);return;}
 int ox=game.x,oy=game.y;uint8_t ex[MAX_ENEMIES],ey[MAX_ENEMIES];
 int px[4],py[4];for(unsigned i=0;i<4;i++){px[i]=coop_players[i].x;py[i]=coop_players[i].y;}
 for(unsigned i=0;i<MAX_ENEMIES;i++){ex[i]=game.enemies[i].x;ey[i]=game.enemies[i].y;}
 steps=(keys[0]>>2)&3;
 unsigned old_coop_ticks=coop_ticks,local_revived=was_dead&&game.hp>0;
 unsigned old_hero_level=hero_progress.level;CoopVisualState visual=coop_visual_capture();
 for(unsigned step=0;step<steps;step++){
  int16_t hp[MAX_ENEMIES];uint8_t alive[MAX_ENEMIES],attack[MAX_ENEMIES];
  for(unsigned i=0;i<MAX_ENEMIES;i++){hp[i]=game.enemies[i].hp;alive[i]=game.enemies[i].alive;attack[i]=game.enemy_attack[i];}
  int16_t player_hp[4];for(unsigned i=0;i<4;i++)player_hp[i]=coop_players[i].hero.hp;
  unsigned tick_before=coop_ticks;coop_sim_step(&game,keys);
  if(player_hp[coop_id]<=0&&coop_players[coop_id].hero.hp>0)local_revived=1;
  if(coop_region_pending()){
   CoopRegionRequest request=coop_region_request;coop_region_clear();
   dialogue_end();inventory_release();shop_cancel_pending();coop_control_command=0;quick_mode=menu_mode=0;
   /* Present the destination before generation/save barriers, as in solo. */
   audio_stop();loading_begin(loading_destination(request.destination==0,request.destination));
   int result=coop_campaign_travel(request.destination,request.x,request.y);
   if(result&&request.owner>=0)game_spell_portal_consume((unsigned)request.owner);
   coop_checksum(&game);coop_scene_resume();
   if(result!=COOP_CAMPAIGN_APPLIED)coop_pause(result==COOP_CAMPAIGN_COMMITTED_LOST?"MOVE SAVED LINK LOST":"MOVE FAILED A SAVE EXIT");
   return;
  }
  if(game.won)break;
  if(coop_ticks!=tick_before)coop_melee_audio();
  if(!game.area)for(unsigned i=0;i<MAX_ENEMIES;i++)if(alive[i]==1){
   int dx=game.enemies[i].x-game.x,dy=game.enemies[i].y-game.y;
   if(dx<-6||dx>6||dy<-6||dy>6)continue;
   if(game.enemies[i].hp<hp[i]){if(game.enemies[i].kind!=MONSTER_DIABLO||game.enemies[i].alive)audio_monster(game.enemies[i].kind,game.enemies[i].alive?1:2);}
   else if(!attack[i]&&game.enemy_attack[i])audio_monster(game.enemies[i].kind,0);
  }
  for(unsigned n=1;n<=4;n++){
   unsigned i=(coop_id+n)&3;const CoopPlayer *p=&coop_players[i];int dx=p->x-game.x,dy=p->y-game.y;
   if((coop_members&(1u<<i))&&player_hp[i]>0&&p->hero.hp<=0&&dx>=-6&&dx<=6&&dy>=-6&&dy<=6)audio_hero_death(p->hero.reserved&255);
  }
  /* Keep this tick's loot in sync with the local death cue. */
  item_sound_update();
 }
 /* Full state validation every32 simulation ticks; command/region barriers remain immediate. */
 if((old_coop_ticks>>5)!=(coop_ticks>>5))coop_checksum(&game);
 coop_visual_refresh(&visual);
 if(hero_progress.level>old_hero_level)audio_effect(41,5);
 move_to(&motion[0],game.x,game.y,ox,oy,WALK_TICKS*3);motion[0].dir=game.facing;
 for(unsigned i=0;i<4;i++){move_to(&coop_motion[i],coop_players[i].x,coop_players[i].y,px[i],py[i],WALK_TICKS*3);coop_motion[i].dir=coop_players[i].facing;}
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].alive==1)move_to(&motion[i+1],game.enemies[i].x,game.enemies[i].y,ex[i],ey[i],game_enemy_walk_ticks(&game,(int)i)*3);
 for(unsigned tick=0;tick<3*steps;tick++){
  for(unsigned i=0;i<MAX_ENEMIES+1;i++)if(!i||(game.enemies[i-1].alive==1&&!game_enemy_hit(i-1)&&!game_spell_stoned(i-1)))advance(&motion[i]);for(unsigned i=0;i<4;i++)advance(&coop_motion[i]);
 }
 coop_render_frame+=3*steps;
 if(!was_dead&&game.hp<=0)coop_death_menu();
 if(local_revived){
  /* Restore presentation only: a one-peer region reload stalls the link and
   * reset_visuals also clears shared projectiles/AI. Keep world state intact. */
  shop_cancel_pending();coop_control_command=0;quick_mode=0;menu_close();
  /* The transparent death menu bypasses inventory_art_active, but borrows
   * OBJ832..839 for its 8bpp cursor and OBJ840 for the cursor palette LUT.
   * Return both loans even when equipment (and its warning key) is unchanged. */
  info_backdrop_restore();durability_hud_key=~0u;
  death_red_ready=0;map_last_x=-1;map_init=1;map_pending=0;
  for(unsigned i=0;i<WORLD_TILES;i++)terrain_invalidate(i);
  ++crowd_mask_epoch;for(unsigned i=0;i<13;i++)last_frame[i]=-1;
  effect_tiles_invalidate();actor_palette_reset();apply_gamma();
  place(&motion[0],game.x,game.y);
 }
 profile_time(4,profile_sim);
 if(!quick_mode&&!menu_mode)draw(coop_render_frame);
 else game_explore_update(&game);
 if(menu_mode){if(menu_mode<NPC_LANDING||menu_mode>NPC_SPEECH||dialogue_dirty)menu_draw();}else quick_draw();graphics_frames++;
}
