/* Paused quick-slot editor. Publish a complete canvas in VBlank so neither
 * idle frames nor cursor changes clear visible text while it is being drawn. */
static UI_FN unsigned spell_picker_list(uint8_t list[SPELL_CHOICES]){
 unsigned n=0;list[n++]=SPELL_SKILL;
 for(unsigned s=1;s<SPELL_CHOICES;s++)if(s!=SPELL_SKILL&&game_spell_available(s))list[n++]=(uint8_t)s;
 list[n++]=0;return n;
}
static UI_FN const char *spell_picker_description(unsigned choice){
 if(!choice)return "Clear the selected quick slot";
 if(choice==SPELL_SKILL){static const char *skills[]={"Repair an equipped item","Disarm a nearby trap","Recharge an equipped staff"};return skills[game_hero_class()];}
 static const char *const descriptions[SPELL_MAX+1]={
  [1]="Launch a bolt of fire",[2]="Restore your life",[3]="Strike foes with lightning",[4]="Blast nearby enemies",
  [6]="Create a wall of fire",[7]="Open a portal to town",[8]="Turn an enemy to stone",[9]="See monsters in the dark",
  [10]="Teleport a short distance",[11]="Use mana to absorb damage",[12]="Launch an exploding fireball",
  [13]="Summon a fire-breathing guardian",[14]="Strike several foes with lightning",[15]="Send a wave of flame",
  [18]="Blast nearby foes with lightning",[20]="Breathe a stream of fire",[21]="Summon a fighting golem",
  [23]="Teleport to a nearby location",[24]="Damage surrounding enemies",[29]="Send a seeking fire elemental",
  [30]="Release charged lightning bolts",[31]="Strike undead with holy power",[32]="Revive another player",
  [33]="Interact with distant objects",[34]="Restore another player's life",[35]="Launch a star of blood",[36]="Drain a third of enemy life"};
 unsigned id=game_spell_id(choice);return id<=SPELL_MAX&&descriptions[id]?descriptions[id]:"Spell unavailable";
}
static UI_FN void spell_picker_icon(uint8_t *p,unsigned top,unsigned choice,unsigned x,unsigned y,unsigned highlight){
 if(y+24<=top||y>=top+8)return;
 unsigned key=game_spell_icon_key(&game,choice),type=key>>8;if(type>4)type=4;
 const uint8_t *icon=(const uint8_t*)spell_flat_icons[key&255],*lut=spellbook_icon_lut[!game.area][type];
 for(unsigned py=top;py<top+8;py++)if(py>=y&&py<y+24)for(unsigned px=0;px<24;px++){
  unsigned yy=py-y,at=(yy/8)*96+(px/8)*32+(yy&7)*4+(px&7)/2,c=(icon[at]>>((px&1)*4))&15;
  if(c)dialogue_pixel(p,x+px,py,top,lut[c]);
 }
 if(highlight){quick_rect(p,top,x,y,24,24,highlight,1);quick_rect(p,top,x+1,y+1,22,22,highlight,1);}
}
static uint8_t spell_picker_grid;
/* The staged canvas survives while paused. Repaint only the two selected
 * icons for each flash, preserving all labels and avoiding a full layout pass. */
static UI_FN void spell_picker_flash(unsigned focus){
 for(unsigned n=0;n<2;n++){
  unsigned index=n?spell_picker_grid:hero_progress.spell_active_slot;
  unsigned x=n?28+index%6*32:60+index*32,y=n?65+index/6*25:22;
  unsigned choice=n?spell_choice:hero_progress.spell_slot[index];
  for(unsigned row=y/8-1;row<=(y+23)/8-1;row++){
   unsigned top=(row+1)*8;uint8_t *p=(uint8_t*)load_buffer;
   dma((const void*)(0x06010000+row*NPC_WIDTH*8),load_buffer,NPC_WIDTH*8);
   quick_rect(p,top,x,y,24,24,0,0);spell_picker_icon(p,top,choice,x,y,focus);
   panel_stage(row);
  }
 }
 panel_publish();
}
static UI_FN void spell_picker_draw(void){
 /* Timer0 runs at16.384kHz. Off/on/off/on gives two synchronized flashes
  * without blanking any text. Time each phase from publication so slow
  * canvas composition cannot skip either blink. Idle frames retain the canvas. */
 if(spell_blink_active&&(uint16_t)(audio_game_clock()-spell_blink_start)>=1966){
  if(++spell_blink_active>4)spell_blink_active=0;
 }
 unsigned blink=spell_blink_active;
 unsigned notice=quick_notice|(blink<<8);
 uint32_t revision=inventory_revision()^((uint32_t)game.mana<<8)^((uint32_t)spell_choice<<24)^hero_progress.spell_active_slot;
 for(unsigned i=0;i<4;i++)revision=revision*16777619u^hero_progress.spell_slot[i];
 if(quick_painted==2&&quick_painted_xp==revision&&(quick_painted_notice&255)==quick_notice){
  if(quick_painted_notice!=notice){
   spell_picker_flash(blink&1?0:quick_ink(31|(26<<5)));
   spell_blink_start=audio_game_clock();quick_painted_notice=notice;
  }
  return;
 }
 uint8_t list[SPELL_CHOICES],lut[32];unsigned count=spell_picker_list(list),selected=0;
 for(unsigned i=0;i<count;i++)if(list[i]==spell_choice)selected=i;
 spell_choice=list[selected];spell_picker_grid=selected%12;unsigned first=selected/12*12;
 panel_begin(lut);unsigned white=quick_shades[0][0],gold=quick_shades[1][0],action=quick_shades[3][0],focus=blink&1?0:quick_ink(31|(26<<5));
 for(unsigned row=0;row<18;row++){
  unsigned top=(row+1)*8;uint8_t *p=(uint8_t*)load_buffer;
  for(unsigned i=0;i<NPC_WIDTH*8/4;i++)load_buffer[i]=0;
  quick_rect(p,top,8,8,224,144,gold,1);
  panel_center(p,top,12,"Quick spell slot",gold,0);
  for(unsigned i=0;i<4;i++){
   spell_picker_icon(p,top,hero_progress.spell_slot[i],60+i*32,22,i==hero_progress.spell_active_slot?focus:0);
  }
  panel_center(p,top,55,"Available spells",gold,0);
  for(unsigned i=first;i<count&&i<first+12;i++)spell_picker_icon(p,top,list[i],28+(i-first)%6*32,65+(i-first)/6*25,i==selected?focus:0);
  dialogue_scrollbar(p,top,65,49,12,count,first,gold,white);
  panel_center(p,top,116,game_spell_name(spell_choice),action,0);
  panel_center(p,top,126,spell_picker_description(spell_choice),white,1);
  quick_text(p,top,16,136,"L/R: select quick slot",white,2);quick_text(p,top,176,136,"B: close",white,2);
  quick_text(p,top,16,145,"A: assign spell to selected slot",white,2);
  panel_stage(row);
 }
 panel_publish();if(blink)spell_blink_start=audio_game_clock();quick_painted=2;quick_painted_xp=revision;quick_painted_notice=notice;
}
static UI_FN void spell_picker_input(unsigned pressed){
 if(!pressed)return;
 spell_blink_active=0;
 unsigned direction=pressed&KEY_UP?0:pressed&KEY_RIGHT?1:pressed&KEY_DOWN?2:pressed&KEY_LEFT?3:4;
 if(pressed&KEY_B){quick_mode=0;quick_clear();return;}
 if(pressed&(KEY_L|KEY_R)){
  unsigned slot=(hero_progress.spell_active_slot+((pressed&KEY_L)?3:1))%4;
  /* Activate the chosen casting slot immediately; A commits the grid choice. */
  if(coop_active)inventory_command=game_spell_wire(1,slot,0);else game_spell_select_slot(slot);
 }else if(direction<4){
  uint8_t list[SPELL_CHOICES];unsigned count=spell_picker_list(list),index=0;
  for(unsigned i=0;i<count;i++)if(list[i]==spell_choice)index=i;
  if(direction==0&&index>=6)index-=6;else if(direction==1&&index+1<count)index++;
  else if(direction==2&&index+6<count)index+=6;else if(direction==3&&index)index--;
  spell_choice=list[index];
 }else if(pressed&KEY_A){
  unsigned slot=hero_progress.spell_active_slot;
  if(spell_choice&&!game_spell_available(spell_choice))quick_notice=1;
  else if(coop_active){inventory_command=game_spell_wire(2,slot,spell_choice);quick_notice=2;}
  else quick_notice=game_spell_bind_slot(slot,spell_choice)?2:1;
  if(quick_notice==2){spell_blink_start=audio_game_clock();spell_blink_active=1;}
  quick_painted=0;return;
 }
 quick_notice=0;
}
