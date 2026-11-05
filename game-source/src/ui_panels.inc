/* OPT127: original Diablo panels adapted to 240x160. Entire paused canvases
 * are staged in OBJ VRAM and published together; gameplay restores every
 * borrowed tile through inventory_release(). */
#include "../generated/ui127_assets.h"
extern const uint8_t asset_front_palette[];
#include "../generated/ui127_cursor.h"
static uint32_t panel_key;
static UI_FN void panel_begin(uint8_t lut[32]){
 lighting_hardware_stop();map_pending=0;frame_state&=~1u;hud_font_mode(0);quick_style();
 for(unsigned i=0;i<32;i++)lut[i]=(uint8_t)quick_ink(ui127_palette[i]);
 R16(0x04000000)&=~0x1000u;for(unsigned i=0;i<128;i++)R16(0x07000000+i*8)=0x0200;
}
static UI_FN void panel_strip(unsigned row,unsigned art,const uint8_t lut[32]){
 uint8_t *p=(uint8_t*)load_buffer;
 lz_block(asset_ui127_panels,art*18+row,p,NPC_WIDTH*8);
 for(unsigned i=0;i<NPC_WIDTH*8;i++)p[i]=lut[p[i]];
}
static UI_FN void panel_stage(unsigned row){dma(load_buffer,(void*)(0x06010000+row*NPC_WIDTH*8),NPC_WIDTH*8);}
static UI_FN void panel_publish(void){
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 dma((const void*)0x06010000,(void*)0x06000000,NPC_CANVAS_TILES*64);
 for(unsigned i=0;i<32;i++)R16(0x06000000+640*64+i*2)=0;
 for(unsigned i=0;i<640;i++)hud_map[i]=640;
 for(unsigned row=1;row<=18;row++)for(unsigned col=1;col<=28;col++)hud_map[row*32+col]=(uint16_t)((row-1)*28+col-1);
 R16(0x0400000a)=0x1b80;R16(0x04000000)=0x0240;R16(0x04000050)=0;
 inventory_art_active=2;
}
static UI_FN void panel_center(uint8_t *p,unsigned top,unsigned y,const char *text,unsigned color,unsigned tiny){if(y+(tiny?6:8)<=top||y>=top+8)return;quick_text(p,top,120-quick_text_width(text,tiny)/2,y,text,color,tiny);}
/* Same 10x10 eight-frame artwork as the frontend, mapped to the live palette. */
static UI_FN void panel_cursor(unsigned y,unsigned frame,unsigned left,unsigned right){
 uint16_t palette[16];
 for(unsigned i=0;i<16;i++){unsigned v=panel_cursor_palette[i],out=0;for(unsigned shift=0;shift<15;shift+=5){unsigned c=((v>>shift)&31)*(8+gamma_level)/10;out|=(c>31?31:c)<<shift;}palette[i]=(uint16_t)out;}
 dma(palette,(void*)(0x05000200+15*32),32);dma(panel_cursor_tiles[frame&7],(void*)0x06010000,128);
 R16(0x07000000)=y;R16(0x07000002)=left|0x4000;R16(0x07000004)=15<<12;
 R16(0x07000008)=y;R16(0x0700000a)=right|0x4000;R16(0x0700000c)=15<<12;
 R16(0x04000000)=0x1240;
}
/* Option rows start on an 8-pixel boundary so every line, including its
 * shadow, fits one tile row in the bounded death-menu dictionary. */
static UI_FN void menu_cursor(unsigned frame){
 if(menu_mode==1)panel_cursor((coop_active?57:46)+menu_choice*11*(coop_active?2:1),frame,43,187);
 else if(menu_mode==2)panel_cursor(47+(menu_choice-(coop_active?1:0))*16,frame,22,215);
 else if(menu_mode==15)panel_cursor(71+menu_choice*18,frame,43,187);
}
/* Death keeps the frozen dungeon beneath a transparent font overlay.
 * Do not publish a full paused canvas: that would replace the red world. */
/* The hidden information-backdrop tiles832..840 hold an8bpp selector and
 * its LUT. Use the frozen scene's existing OBJ palette, including 8bpp corpses:
 * never overwrite hero graphics or any live palette bank for this overlay. */
static UI_FN __attribute__((minsize)) void death_cursor_prepare(void){
 for(unsigned c=0;c<16;c++){
  unsigned target=actor_palette_gamma(panel_cursor_palette[c],gamma_level),best=~0u,at=1;
  for(unsigned i=1;i<256;i++){
   unsigned rgb=R16(0x05000200+i*2);int r=(int)(rgb&31)-(int)(target&31),g=(int)((rgb>>5)&31)-(int)((target>>5)&31),b=(int)(rgb>>10)-(int)(target>>10);
   unsigned d=r*r+g*g+b*b;if(d<best){best=d;at=i;}
  }
  R16(0x06010000+840*32+c*2)=c?at:0;
 }
}
static UI_FN __attribute__((minsize)) void death_menu_cursor(void){
 if(menu_mode!=1&&menu_mode!=2&&menu_mode!=15)return;
 unsigned frame=(audio_game_clock()/1640)&7,y=menu_mode==1?39+menu_choice*16:menu_mode==2?47+(menu_choice-(coop_active?1:0))*16:71+menu_choice*18;
 const uint8_t *src=panel_cursor_tiles[frame];
 for(unsigned i=0;i<128;i++)R16(0x06010000+832*32+i*2)=R16(0x06010000+840*32+(src[i]&15)*2)|(R16(0x06010000+840*32+(src[i]>>4)*2)<<8);
 for(unsigned side=0;side<2;side++){
  unsigned at=0x07000000+(126+side)*8;R16(at)=y|0x2000;R16(at+2)=(side?(coop_active?220:menu_mode==2?215:187):(coop_active?4:menu_mode==2?22:43))|0x4000;R16(at+4)=832;
 }
}
/* Compact transparent77-font tiles occupy only the idle automap/font cells.
 * Stage both dictionary and map in scratch; publish in one VBlank without
 * disabling the frozen world. Never borrow terrain or live actor graphics. */
static UI_FN unsigned death_text_tile(unsigned n){return n<87?265+n:n<109?133+n-87:163+n-109;}
static UI_FN __attribute__((target("thumb"),minsize)) void death_menu_draw(void){
 unsigned key=0xd0000000u|menu_mode|(confirm_action<<8)|(gamma_level<<16)|(audio_music_volume<<12)|(audio_effects_volume<<21)|(dpad_rotation<<25);
 if(death_red_ready&&panel_key==key){death_menu_cursor();return;}
 if(menu_mode==1&&!menu_choice)menu_choice=1;
 info_stream_stop();lighting_hardware_stop();map_pending=0;map_init=1;frame_state&=~1u;
 /* BG2 must stay off: its streamed dictionary becomes the menu overlay. */
 R16(0x04000000)&=(uint16_t)~0x2c00u;
 death_red_back();death_cursor_prepare();quick_style();
 uint8_t *tiles=(uint8_t*)load_buffer+2048;uint16_t *map=(uint16_t*)((uint8_t*)load_buffer+10240);
 for(unsigned i=0;i<640;i++)map[i]=128;
 for(unsigned y=16;y<20;y++)for(unsigned x=0;x<30;x++)if(x<4||x>=26)map[y*32+x]=129+(y-16)*30+x;
 unsigned used=0,white=quick_shades[0][0],gold=quick_shades[1][0],gray=quick_shades[2][0],action=quick_shades[3][0];
 for(unsigned row=3;row<19;row++){
  unsigned top=row*8;uint8_t *p=(uint8_t*)load_buffer;
  for(unsigned i=0;i<NPC_WIDTH*8/4;i++)load_buffer[i]=0;
  if(menu_mode==1){
   const char *items[]={"Save Game","Options","New Game","Load Game","Quit Diablo"};
   const char *coop_items[]={"Save Game","Options","Restart at dungeon entrance","Save and leave"};
   for(unsigned i=0;i<(coop_active?4u:5u);i++)panel_center(p,top,40+i*16,coop_active?coop_items[i]:items[i],i? action:gray,0);
  }else if(menu_mode==2){
   const char *items[]={"Music","Sound","Gamma","D-pad Rotation","Previous Menu"};unsigned levels[]={audio_music_volume,audio_effects_volume,gamma_level+1};
   for(unsigned i=coop_active?1:0;i<5;i++){
    unsigned y=48+(i-(coop_active?1:0))*16;
    quick_text(p,top,39,y,items[i],action,0);
    if(i<3){char value[12],*end=quick_number(value,levels[i]);end=quick_append(end,i==2?"/20":"/10");*end=0;quick_text(p,top,181,y,value,gold,0);}
    else if(i==3)quick_text(p,top,210-quick_text_width(options_dpad_labels[dpad_rotation],1),y+1,options_dpad_labels[dpad_rotation],gold,1);
   }
  }else if(menu_mode==15){
   panel_center(p,top,50,"Difficulty",gold,0);
   for(unsigned i=0;i<GAME_DIFFICULTY_COUNT;i++)panel_center(p,top,72+i*18,game_difficulty_name(i),action,0);
  }else if(menu_mode==3){
   panel_center(p,top,56,confirm_action==2?"Start New Game?":confirm_action==3?"Load Saved Game?":"Return to Title?",gold,0);
   panel_center(p,top,128,"B: cancel  A: Ok",white,0);
  }else panel_center(p,top,56,menu_notice?menu_notice:"Save unavailable",white,0);
  for(unsigned col=0;col<28;col++){
   uint32_t *cell=load_buffer+col*16;unsigned bits=0;for(unsigned i=0;i<16;i++)bits|=cell[i];
   if(bits&&used<127){for(unsigned i=0;i<64;i++)tiles[used*64+i]=p[col*64+i];map[row*32+col+1]=(uint16_t)death_text_tile(used++);}
  }
 }
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 for(unsigned i=0;i<32;i++)R16(0x0600a000+i*2)=0;
 for(unsigned i=0;i<used;i++)dma(tiles+i*64,(void*)(0x06008000+death_text_tile(i)*64),64);
 dma(map,(void*)hud_map,1280);
 /* Hide minimap arrows and information backdrops, retain belt/world actors. */
 for(unsigned i=0;i<42;i++)if(i<4||i>=14)R16(0x07000000+i*8)=0x0200;
 R16(0x07000000+126*8)=R16(0x07000000+127*8)=0x0200;
 hud_transparent=0;R16(0x04000050)=0;R16(0x04000054)=0;R16(0x0400000a)=0x1b88;R16(0x04000000)=0x1340;
 panel_key=key;death_menu_cursor();
}
static UI_FN __attribute__((minsize)) void menu_canvas_draw(void){
 if(active_game&&game.hp<=0){death_menu_draw();return;}
 if(menu_mode==6){
  CharacterState h;game_get_character(&game,&h);uint32_t key=status_revision(&h)^(menu_choice<<24);
  if(inventory_art_active==2&&panel_key==key)return;
  status_draw(&h,menu_choice,0);panel_key=key;return;
 }
 unsigned frame=(audio_game_clock()/1640)&7;
 uint32_t key=menu_mode|(audio_music_volume<<12)|(audio_effects_volume<<16)|(gamma_level<<20)|(dpad_rotation<<25)|(game.hp<=0?1u<<28:0);
 if(inventory_art_active==2&&panel_key==key){menu_cursor(frame);return;}
 if(menu_mode==1&&game.hp<=0&&!menu_choice)menu_choice=1;
 uint8_t lut[32];panel_begin(lut);
 unsigned white=quick_shades[0][0],gold=quick_shades[1][0],gray=quick_shades[2][0],action=quick_shades[3][0];
 for(unsigned row=0;row<18;row++){
  unsigned top=(row+1)*8;uint8_t *p=(uint8_t*)load_buffer;panel_strip(row,2,lut);
  quick_rect(p,top,8,8,224,144,gold,1);
  if(menu_mode==1){
   const char *items[]={"Save Game","Options","New Game","Load Game","Quit Diablo"};
   const char *coop_items[]={"Options","Save and leave"};
   for(unsigned i=0;i<(coop_active?2u:5u);i++)panel_center(p,top,(coop_active?58:47)+i*11*(coop_active?2:1),coop_active?coop_items[i]:items[i],i==0&&game.hp<=0?gray:action,0);

   quick_text(p,top,16,104,"L: health potion",white,1);quick_text(p,top,128,104,"R: mana potion",white,1);
   quick_text(p,top,16,120,"DPAD: move",white,1);quick_text(p,top,128,120,"A: attack, pick up, talk",white,1);
   quick_text(p,top,128,130,"B: cast spell, cancel",white,1);
   quick_text(p,top,16,144,"Select: quick spell",white,1);quick_text(p,top,128,144,"Start: menu shortcut",white,1);

  }else if(menu_mode==2){
   const char *items[]={"Music","Sound","Gamma","D-pad Rotation","Previous Menu"};
   unsigned levels[]={audio_music_volume,audio_effects_volume,gamma_level+1};
   for(unsigned i=coop_active?1:0;i<5;i++){
    unsigned y=48+(i-(coop_active?1:0))*16;
    quick_text(p,top,39,y,items[i],action,0);
    if(i<3){char value[12],*end=quick_number(value,levels[i]);end=quick_append(end,i==2?"/20":"/10");*end=0;quick_text(p,top,181,y,value,gold,0);}
    else if(i==3)quick_text(p,top,210-quick_text_width(options_dpad_labels[dpad_rotation],1),y+1,options_dpad_labels[dpad_rotation],gold,1);
   }

   quick_text(p,top,16,139,"Dpad: adjust  B: back  A: select",white,1);
  }else if(menu_mode==15){
   panel_center(p,top,50,"Difficulty",gold,0);
   for(unsigned i=0;i<GAME_DIFFICULTY_COUNT;i++)panel_center(p,top,72+i*18,game_difficulty_name(i),action,0);

   panel_center(p,top,139,"B: back  A: select",white,1);
  }else if(menu_mode==3){
   panel_center(p,top,62,confirm_action==2?"Start New Game?":confirm_action==3?"Load Saved Game?":"Return to Title?",gold,0);
   if(confirm_action==2){panel_center(p,top,80,game_difficulty_name(new_difficulty),white,0);panel_center(p,top,101,"Keep hero and items",white,0);panel_center(p,top,116,"Reset map and save",white,0);}
   else panel_center(p,top,94,"Unsaved progress will be lost",white,0);
   panel_center(p,top,139,"B: cancel  A: Ok",white,0);
  }else{
   panel_center(p,top,71,menu_mode==4?menu_notice:session_hero?"Game closed":"Saved hero found",white,0);
   if(menu_mode==4&&flash_status&&flash_status!=4){char value[24],*end=quick_append(value,"Save code ");end=quick_number(end,flash_status);*end=0;panel_center(p,top,94,value,gold,0);}
   panel_center(p,top,139,"B: back  A: continue",white,1);
  }
  panel_stage(row);
 }
 panel_publish();panel_key=key;menu_cursor(frame);
}
/* DrawChr: retain the two-column grouping, base/current stats, combat and
 * resistance values. Original stone and gold frame; native-resolution type. */
static UI_FN void status_value(uint8_t *p,unsigned top,unsigned x,unsigned y,unsigned w,const char *s,unsigned ink,unsigned edge){
 if(y+10<=top||y>=top+8)return;
 quick_rect(p,top,x,y,w,10,0,0);quick_rect(p,top,x,y,w,10,edge,1);
 quick_text(p,top,x+(w-quick_text_width(s,1))/2,y+2,s,ink,1);
}
static UI_FN __attribute__((minsize)) void status_draw(const CharacterState *h,unsigned selected,int readonly){
 const HeroProgress *pr=&h->progress;unsigned cls=h->reserved&255;if(cls>2)cls=0;
 ItemEffects e;item_build_effects(pr,&h->inventory,cls,&e);
 const char *classes[]={"Warrior","Rogue","Sorcerer"};const char *labels[]={"Strength","Magic","Dexterity","Vitality"};
 char values[22][16];unsigned nums[22]={pr->level,pr->experience,game_next_experience(pr->level),h->gold>0?(unsigned)h->gold:0,
 pr->strength,e.stats[0],pr->magic,e.stats[1],pr->dexterity,e.stats[2],pr->vitality,e.stats[3],pr->points,
 h->hp>0?(unsigned)h->hp:0,e.max_hp,h->mana>0?(unsigned)h->mana:0,e.max_mana,0,0,0,e.resist[2],e.resist[0]};
 for(unsigned i=0;i<22;i++)values[i][info_uint(values[i],nums[i])]=0;
 if(pr->level>=50){values[2][0]='M';values[2][1]='A';values[2][2]='X';values[2][3]=0;}
 unsigned bonus=game_attribute_damage(pr,&e,cls);
 int low=e.weapon_low,high=e.weapon_high;low+=low*e.damage_percent/100+e.damage_flat+bonus;high+=high*e.damage_percent/100+e.damage_flat+bonus;if(low<1)low=1;if(high<low)high=low;
 char damage[16],life[16],mana[16],armor[12],aim[12],resist[3][8];char *at=quick_number(damage,low);*at++='-';at=quick_number(at,high);*at=0;
 at=quick_number(life,nums[13]);*at++='/';at=quick_number(at,nums[14]);*at=0;
 at=quick_number(mana,nums[15]);*at++='/';at=quick_number(at,nums[16]);*at=0;
 int ac=e.armor+e.stats[2]/5;at=armor;if(ac<0){*at++='-';ac=-ac;}*quick_number(at,(unsigned)ac)=0;
 unsigned bow=e.weapon_flags&ITEM_BOW;int hit=pr->level+(bow?e.stats[2]:e.stats[2]/2)+50+(bow?(cls==0?10:cls==1?20:0):(cls==0?20:0))+e.to_hit;
 *quick_number(aim,hit>0?(unsigned)hit:0)=0;
 for(unsigned i=0;i<3;i++){unsigned r=e.resist[i==0?2:i==1?0:1];char *end=quick_number(resist[i],r);*end++='%';*end=0;}
 uint8_t lut[32],button_lut[UI128_COLORS];panel_begin(lut);unsigned white=quick_shades[0][0],gold=quick_shades[1][0],red=hud_color_index(HUD_RED);
 for(unsigned i=0;i<UI128_COLORS;i++)button_lut[i]=quick_ink(ui128_palette[i]);
 for(unsigned row=0;row<18;row++){
  unsigned top=(row+1)*8;uint8_t *p=(uint8_t*)load_buffer;panel_strip(row,0,lut);
  quick_rect(p,top,16,14,208,13,0,0);quick_text(p,top,22,17,"Character",gold,0);quick_text(p,top,222-quick_text_width(classes[cls],0),17,classes[cls],white,0);
  if(!readonly)quick_text(p,top,108,18,"A: +1",white,2);
  quick_text(p,top,19,34,"Level",white,1);status_value(p,top,51,31,25,values[0],white,gold);
  quick_text(p,top,126,34,"Experience",white,1);status_value(p,top,171,31,50,values[1],white,gold);
  quick_text(p,top,126,46,"Next level",white,1);status_value(p,top,171,43,50,values[2],white,gold);
  quick_text(p,top,126,58,"Gold",white,1);status_value(p,top,171,55,50,values[3],white,gold);
  quick_text(p,top,68,48,"Base",gold,1);quick_text(p,top,94,48,"Now",gold,1);
  for(unsigned i=0;i<4;i++){
   unsigned y=58+i*13;quick_text(p,top,19,y,labels[i],white,1);
   status_value(p,top,64,y-2,23,values[4+i*2],white,gold);status_value(p,top,90,y-2,23,values[5+i*2],white,gold);
   if(!readonly&&i==selected)quick_rect(p,top,17,y-3,97,12,gold,1);
   if(!readonly&&pr->points&&h->hp>0&&nums[4+i*2]<game_stat_limit(cls,i))
    for(unsigned by=0;by<10;by++)for(unsigned bx=0;bx<10;bx++)dialogue_pixel(p,114+bx,y-2+by,top,button_lut[ui128_plus[by*10+bx]]);
  }
  quick_text(p,top,19,110,"Points",white,1);status_value(p,top,64,107,49,values[12],pr->points?red:white,gold);
  quick_text(p,top,19,123,"Life",white,1);status_value(p,top,51,120,62,life,white,gold);
  quick_text(p,top,19,136,"Mana",white,1);status_value(p,top,51,133,62,mana,white,gold);
  const char *right[]={"Armor class","To hit","Damage","Resist magic","Resist fire","Resist light"};
  const char *rv[]={armor,aim,damage,resist[0],resist[1],resist[2]};
  for(unsigned i=0;i<5;i++){unsigned y=72+i*13;quick_text(p,top,124,y,right[i],white,1);status_value(p,top,175,y-2,46,rv[i],white,gold);}
  panel_stage(row);
 }
 panel_publish();
}
static UI_FN __attribute__((minsize)) unsigned inventory_detail_add(char lines[24][24],unsigned count,const char *s,unsigned width){
 while(*s&&count<24){unsigned n=0,last=0;while(s[n]&&n<width){if(s[n]==' ')last=n;n++;}if(s[n]&&last)n=last;
  for(unsigned i=0;i<n;i++)lines[count][i]=s[i];lines[count++][n]=0;s+=n;while(*s==' ')s++;
 }return count;
}
/* Reuse the complete affix formatter, without its bottom-HUD five-row cap.
 * Preserve that HUD's prepared text and keep all details in transient memory. */
static UI_FN __attribute__((minsize)) unsigned inventory_unique_format(const Item *it,char lines[24][24],unsigned width,unsigned *names){
 char saved[5][23],name[64];unsigned saved_count=info_count,saved_style=info_name_style,count=0;
 for(unsigned i=0;i<sizeof(saved);i++)((char*)saved)[i]=((char*)info_lines)[i];
 item_name(it,name);count=inventory_detail_add(lines,count,name,width);*names=count;
 if(item_unidentified(it))count=inventory_detail_add(lines,count,"NOT IDENTIFIED",width);
 else for(unsigned i=0;i<item_power_count(it);i++){info_reset();info_affix(it,i);
  for(unsigned row=0;row<info_count;row++)count=inventory_detail_add(lines,count,info_lines[row],width);
 }
 for(unsigned i=0;i<sizeof(saved);i++)((char*)info_lines)[i]=((char*)saved)[i];info_count=saved_count;info_name_style=saved_style;return count;
}
/* OBJ512..831 is free beside the live inventory canvas. OBJ832 onward
 * holds the info backdrop, belt and HUD: never use it as strip scratch.
 * Stage the last six rows in load_buffer after the strip and border, before
 * the staged HUD map. No extra allocation or live graphics overwrite. */
enum { INVENTORY_CONTROL_ROWS=10, INVENTORY_CONTROL_STRIP=15*64, INVENTORY_CONTROL_RAM=6144 };
_Static_assert(0x4000+INVENTORY_CONTROL_ROWS*INVENTORY_CONTROL_STRIP<=832*32,"inventory staging overlaps live HUD");
_Static_assert(INVENTORY_CONTROL_RAM>=4096+1452&&INVENTORY_CONTROL_RAM+6*INVENTORY_CONTROL_STRIP<=17408,"inventory staging overlaps border or map");
static UI_FN __attribute__((minsize)) void inventory_controls(void){
 quick_style();inventory_frame_prepare();unsigned white=quick_shades[0][0],gold=quick_shades[1][0],height=info_count==5?120:128;
 char details[24][24];unsigned detail_count=0,name_rows=0,detail_font=2;
 int selected=inventory_held();if(selected<0)selected=inventory_selected();
 if(!inventory_confirm&&selected>=0&&selected<ITEM_SLOTS){const Item *it=&inventory_view()->items[selected];if(item_unique(it)){
  detail_count=inventory_unique_format(it,details,17,&name_rows);
  if(detail_count>(height-26)/7){detail_font=1;detail_count=inventory_unique_format(it,details,22,&name_rows);}
 }}
 char question[40]="";if(inventory_confirm==3){char *end=quick_append(question,"Drop ");end=quick_number(end,inventory_gold_amount);end=quick_append(end," gold?");*end=0;}
 for(unsigned row=0;row<height/8;row++){
  unsigned top=row*8;uint8_t *p=(uint8_t*)load_buffer;for(unsigned i=0;i<NPC_WIDTH*8/4;i++)load_buffer[i]=0;
  inventory_frame_strip(p,top,0,120,height);
  quick_text(p,top,68-quick_text_width("Inventory",0)/2,5,"Inventory",gold,0);
  if(inventory_confirm==3){
   const char *lines[]={"Use the D-pad","to select gold","in increments","of 100."};
   for(unsigned i=0;i<4;i++)quick_text(p,top,16,22+i*11,lines[i],white,0);
   quick_text(p,top,16,75,question,gold,0);
   quick_text(p,top,16,98,"B: cancel",white,0);quick_text(p,top,16,112,"A: Ok",white,0);
  }else if(inventory_confirm){
   const char *message=inventory_confirm==6?"Use this item?":inventory_confirm==1?"Drop this item?":"Choose an item";
   const char *keys="B: cancel  A: Ok";
   quick_text(p,top,68-quick_text_width(message,0)/2,32,message,white,0);
   quick_text(p,top,68-quick_text_width(keys,0)/2,96,keys,white,0);
  }else if(detail_count){
   unsigned step=detail_font==2?7:6;
   for(unsigned i=0;i<detail_count;i++)quick_text(p,top,68-quick_text_width(details[i],detail_font)/2,23+i*step,details[i],i<name_rows?gold:white,detail_font);
  }else{
   quick_text(p,top,13,48,"L: drop",white,0);quick_text(p,top,73,48,"R: use",white,0);
   quick_text(p,top,13,72,"B: cancel",white,0);quick_text(p,top,73,72,inventory_page==3?"A: Drop":"A: Pick up",white,0);
   if(inventory_notice==INV_GROUND_FULL||inventory_notice==INV_FULL)quick_text(p,top,16,108,inventory_notice==INV_GROUND_FULL?"No drop space":"Backpack full",gold,1);
  }
  void *stage=row<INVENTORY_CONTROL_ROWS?(void*)(0x06014000+row*INVENTORY_CONTROL_STRIP):(uint8_t*)load_buffer+INVENTORY_CONTROL_RAM+(row-INVENTORY_CONTROL_ROWS)*INVENTORY_CONTROL_STRIP;
  dma(p,stage,INVENTORY_CONTROL_STRIP);
 }
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 dma((void*)0x06014000,(void*)(0x06000000+83*64),INVENTORY_CONTROL_ROWS*INVENTORY_CONTROL_STRIP);
 dma((uint8_t*)load_buffer+INVENTORY_CONTROL_RAM,(void*)(0x06000000+83*64+INVENTORY_CONTROL_ROWS*INVENTORY_CONTROL_STRIP),(height/8-INVENTORY_CONTROL_ROWS)*INVENTORY_CONTROL_STRIP);
 for(unsigned row=0;row<16&&row<20-info_count;row++)for(unsigned col=0;col<15;col++)hud_map[row*32+col]=(uint16_t)(row<height/8?83+row*15+col:640);
}
static UI_FN unsigned questlog_list(uint8_t quests[16]){
 unsigned count=0;if(game_campaign_layout())for(unsigned q=0;q<16;q++)if(dialogue_topic(q))quests[count++]=(uint8_t)q;return count;
}
static UI_FN __attribute__((minsize)) void questlog_draw(void){
 uint8_t quests[16],lut[32];unsigned count=questlog_list(quests);if(quick_notice>count)quick_notice=count;
 unsigned first=quick_notice<count?quick_notice/7*7:count>7?(count-1)/7*7:0;
 unsigned frame=(audio_game_clock()/1640)&7,key=0x71000000|(count<<16)|(first<<8);
 const char *selected=quick_notice<count?town_dialogue_quest_names[quests[quick_notice]]:"B: close quest log";
 unsigned width=quick_text_width(selected,0),cy=quick_notice<count?39+(quick_notice-first)*12:125;
 if(inventory_art_active==2&&panel_key==key){panel_cursor(cy,frame,120-width/2-13,120+width/2+3);return;}
 panel_begin(lut);unsigned white=quick_shades[0][0],gold=quick_shades[1][0],action=quick_shades[3][0];
 for(unsigned row=0;row<18;row++){
  unsigned top=(row+1)*8;uint8_t *p=(uint8_t*)load_buffer;panel_strip(row,1,lut);
  panel_center(p,top,20,"Quest Log",gold,0);
  if(!count)panel_center(p,top,72,"No active quests",white,0);
  for(unsigned i=first;i<count&&i<first+7;i++){
   const char *title=town_dialogue_quest_names[quests[i]];unsigned y=40+(i-first)*12;
   panel_center(p,top,y,title,action,0);
  }
  panel_center(p,top,126,"B: close quest log",white,0);
  dialogue_scrollbar(p,top,39,82,7,count,first,gold,white);
  panel_stage(row);
 }
 panel_publish();panel_key=key;panel_cursor(cy,frame,120-width/2-13,120+width/2+3);
}
static UI_FN void questlog_return(void){
 /* Both views are paused canvases. Do not restore the whole town between
  * them; that needlessly leaves a black frame before rebuilding this list. */
 audio_speech_stop();dialogue_session=dialogue_dirty=dialogue_font_ready=0;
 terrain_invalidate(512);inventory_art_active=0;
 menu_mode=0;quick_mode=6;quick_painted=0;questlog_draw();
}
static UI_FN unsigned questlog_message(unsigned q){
 /* quests.cpp questlist._qdmsg, with the original long prose streams. */
 static const uint16_t messages[16]={
  TOWN_TEXT_INFRA5,TOWN_TEXT_MUSH8,TOWN_DIALOGUE_MESSAGE_COUNT+31,
  TOWN_DIALOGUE_MESSAGE_COUNT+35,TOWN_DIALOGUE_MESSAGE_COUNT+9,TOWN_TEXT_VILE3,
  TOWN_TEXT_BUTCH9,TOWN_TEXT_BANNER2,TOWN_DIALOGUE_MESSAGE_COUNT+8,
  TOWN_DIALOGUE_MESSAGE_COUNT+7,TOWN_TEXT_ANVIL5,TOWN_DIALOGUE_MESSAGE_COUNT+39,
  TOWN_TEXT_KING2,TOWN_TEXT_POISON3,TOWN_DIALOGUE_MESSAGE_COUNT+38,TOWN_TEXT_VILE1};
 return q==1&&quest_progress.stage[1]>=4?TOWN_TEXT_MUSH10:messages[q];

}
static UI_FN void questlog_input(unsigned pressed){
 uint8_t quests[16];unsigned count=questlog_list(quests);if(quick_notice>count)quick_notice=count;
 if(pressed&KEY_UP)quick_notice=(quick_notice+count)%(count+1);
 else if(pressed&KEY_DOWN)quick_notice=(quick_notice+1)%(count+1);
 if(pressed&KEY_B||((pressed&KEY_A)&&quick_notice==count)){quick_mode=0;quick_clear();return;}
 if(pressed&KEY_A){unsigned message=questlog_message(quests[quick_notice]);quick_mode=0;quick_clear();dialogue_session=1;dialogue_font_ready=0;dialogue_say(message,NPC_QUEST_RETURN);menu_draw();}
}
