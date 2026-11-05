#include "inventory.inc"
static void coop_stat_request(unsigned stat);
/* Local UI only: never send popup choices as movement/combat cable input. */
volatile unsigned quick_mode; /* 0 closed,1 navigator,2 spells,4 coop character,5 inventory,6 quests,7 book */
static unsigned spell_choice=1;
static unsigned quick_blocked,quick_notice,quick_painted,quick_painted_notice;
static uint32_t quick_painted_xp;
static uint16_t spell_blink_start;
static uint8_t spell_blink_active,skill_inventory_pending;
static UI_FN void quick_clear(void){spell_blink_active=0;inventory_release();quick_painted=0;menu_clear();map_last_x=-1;map_pending=0;}
#include "quick_canvas.inc"
#include "ui_panels.inc"
#include "spell_picker.inc"
static UI_FN void quick_draw(void){
 if(!quick_mode)return;
 if(quick_mode==2){hud_font_mode(0);spell_picker_draw();return;}
 if(quick_mode==4){
  uint32_t revision=status_revision(&coop_players[coop_id].hero)^(menu_choice<<24);
  if(quick_painted!=4||quick_painted_xp!=revision){status_draw(&coop_players[coop_id].hero,menu_choice,0);quick_painted=4;quick_painted_xp=revision;}return;
 }
 if(quick_mode==6){questlog_draw();return;}
 if(quick_mode==1||quick_mode==7){quick_canvas_draw(quick_mode==7);return;}
 lighting_hardware_stop();
 R16(0x07000000+127*8)=0x0200;R16(0x07000000+126*8)=0x0200;
 hud_font_mode(0);
 if(quick_mode==5){
  map_pending=0;
  uint32_t revision=inventory_revision();
  if(quick_painted!=5||quick_painted_xp!=revision){inventory_draw();quick_painted=5;quick_painted_xp=revision;}
  inventory_present();return;
 }

}
/* Return1 consumes this frame's input.
 * Quarantine every held key until its release, including a selection key held
 * after its popup closed. A simultaneous Start+Select deterministically picks Start. */
static UI_FN unsigned quick_input(unsigned held,unsigned pressed){
 quick_blocked&=held;
 if(!menu_mode&&!quick_mode&&game.swing_target==SPELL_SKILL_SWING&&game.attack_wait)skill_inventory_pending=1;
 if(skill_inventory_pending){
  if(game.hp<=0||game.hit_flash){skill_inventory_pending=0;}
  else if(game.swing_target!=SPELL_SKILL_SWING||!game.attack_wait){
   skill_inventory_pending=0;quick_mode=5;
   inventory_page=inventory_cursor=inventory_slot=inventory_notice=inventory_command=0;inventory_confirm=5;
   quick_blocked|=held;quick_clear();return 1;
  }
  if(skill_inventory_pending){quick_blocked|=held;return 0;}
 }
 if(pressed&KEY_START){
  if(quick_mode==5&&inventory_held()>=0)inventory_drag(127);
  if(menu_mode){menu_close();quick_mode=0;}
  else if(quick_mode>=3)quick_mode=0;
  else quick_mode=quick_mode==1?0:1;
  quick_notice=0;spell_choice=hero_progress.spell_slot[hero_progress.spell_active_slot];quick_blocked|=held;quick_clear();return 1;
 }
 if(!menu_mode&&!quick_mode&&(pressed&KEY_SELECT)){
  /* Cycle assigned slots, including spells temporarily unusable in town or
   * without mana. Empty slots are skipped; no assignments leaves it unchanged. */
  unsigned current=hero_progress.spell_active_slot&3;
  for(unsigned step=1;step<=4;step++){
   unsigned slot=(current+step)&3;
   if(!hero_progress.spell_slot[slot])continue;
   if(slot!=current){
    if(coop_active)inventory_command=game_spell_wire(1,slot,0);else game_spell_select_slot(slot);
   }
   break;
  }
  quick_blocked|=KEY_SELECT;return 1;
 }
 if(!menu_mode&&!quick_mode&&(pressed&KEY_B)&&hero_progress.spell_slot[hero_progress.spell_active_slot]==SPELL_SKILL){
  if(game_hero_class()==1){if(coop_active)inventory_command=ITEM_COMMAND_MAKE(ITEM_SLOTS,1)|ITEM_IDENTIFY_COMMAND;else inventory_notice=game_skill_item(&game,ITEM_SLOTS);}
  else{
   if(game.hp>0&&!game.attack_wait&&!game.hit_flash){return 0;}
  }
  return 1;
 }
 if(!quick_mode)return 0;
 quick_blocked|=held;
 if(quick_mode==1){
  if(pressed&KEY_SELECT){quick_clear();quick_mode=2;quick_notice=0;spell_choice=hero_progress.spell_slot[hero_progress.spell_active_slot];}
  else if(pressed&KEY_UP){quick_clear();quick_mode=0;menu_mode=1;menu_choice=0;menu_draw();}
  else if(pressed&KEY_LEFT){quick_clear();menu_choice=0;quick_mode=coop_active?4:0;if(!coop_active){menu_mode=6;menu_draw();}}
  else if(pressed&KEY_DOWN){map_visible^=1;quick_mode=0;quick_clear();}
  else if(pressed&KEY_L){quick_clear();quick_mode=6;quick_notice=0;}
  else if(pressed&KEY_R){quick_clear();quick_mode=7;quick_notice=0;spellbook_page=spellbook_row=0;}
  else if(pressed&KEY_RIGHT){quick_clear();quick_mode=5;inventory_page=inventory_cursor=inventory_slot=inventory_notice=inventory_confirm=inventory_command=0;}
  else if(pressed&KEY_B){quick_mode=0;quick_clear();}
 }else if(quick_mode==2){spell_picker_input(pressed);}
 else if(quick_mode==4){
  if(pressed&KEY_B){quick_mode=0;quick_clear();}
  else{
   if(pressed&KEY_UP)menu_choice=(menu_choice+3)&3;
   else if(pressed&KEY_DOWN)menu_choice=(menu_choice+1)&3;
   if(pressed&KEY_A)coop_stat_request(menu_choice);
  }
 }
 else if(quick_mode==7){spellbook_input(pressed);}

 else if(quick_mode==5){if((pressed&KEY_B)&&!inventory_confirm&&inventory_held()<0){quick_mode=0;quick_clear();}else{inventory_input(held,pressed);if(!coop_active&&(spell_pending[0]||SPELL_CASTING(game.swing_target))){quick_mode=0;quick_clear();}}}
 else if(quick_mode==6){questlog_input(pressed);}
 else if(pressed&KEY_B){quick_mode=0;quick_clear();}
 return 1;
}
