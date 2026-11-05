/* Retail town speech and read-only Talk navigation. Quest side effects remain
 * in the acknowledged contact path; browsing never calls game_quest_talk. */
#include "../generated/town_dialogue.h"
#include "../generated/quest_prose71.h"
#include "../generated/story_prose76.h"
#include "../generated/town_dialogue_font.h"
#include "../generated/npc_menu_cursor.h"
enum { NPC_LANDING=16,NPC_TOPICS=17,NPC_SPEECH=18,NPC_QUEST_RETURN=19,NPC_COLUMNS=28,
       NPC_WIDTH=224,NPC_TEXT_WIDTH=216,NPC_LINE_STEP=TOWN_DIALOGUE_FONT_INK_HEIGHT+5,NPC_ROWS=100/NPC_LINE_STEP,NPC_CANVAS_TILES=504,NPC_PROSE_COLOR=208 };
static uint16_t dialogue_message,dialogue_row,dialogue_lines;
static uint8_t dialogue_npc,dialogue_choice,dialogue_return;
static uint8_t dialogue_dirty,dialogue_gossip,dialogue_font_ready;
static uint16_t dialogue_repeat_clock;
static uint8_t dialogue_repeat_key,dialogue_repeat_fast;
static UI_FN void menu_draw(void);
static UI_FN void menu_close(void);
static UI_FN void questlog_return(void);
static UI_FN void dialogue_topics_open(unsigned npc);

static UI_FN const char *dialogue_title(unsigned message){
 unsigned n=message-TOWN_DIALOGUE_MESSAGE_COUNT;
 return n>=44&&n<53?story_prose76[n-44].title:n<42?quest_prose71[n].title:"";
}
static UI_FN const char *dialogue_text(unsigned message){
 if(message>=TOWN_DIALOGUE_MESSAGE_COUNT+44&&message<TOWN_DIALOGUE_MESSAGE_COUNT+53)return story_prose76[message-TOWN_DIALOGUE_MESSAGE_COUNT-44].text;
 if(message>=TOWN_DIALOGUE_MESSAGE_COUNT){unsigned n=message-TOWN_DIALOGUE_MESSAGE_COUNT;return n<42&&quest_prose71[n].title?quest_prose71[n].text[game_hero_class()%3]:"";}
 return (const char*)asset_town_dialogue_text+town_dialogue_messages[message].text_offset;
}
/* Keep the approved bitmap; remove its fixed-cell side bearings. Each pair
 * of visible glyph bounds has one clear pixel. A word space advances4px. */
static inline __attribute__((always_inline)) unsigned dialogue_glyph(unsigned ch,unsigned *left){
 if(ch<32||ch>=128)ch='?';
 unsigned bits=0;for(unsigned y=0;y<TOWN_DIALOGUE_FONT_INK_HEIGHT;y++)bits|=town_dialogue_font[ch-32][y];
 if(!bits){*left=0;return 3;}
 unsigned a=0,b=TOWN_DIALOGUE_FONT_INK_WIDTH;while(!(bits&(128u>>a)))a++;while(!(bits&(128u>>(b-1))))b--;
 *left=a;return b-a;
}
/* Pixel-width wrapping retains the complete original prose and punctuation. */
static UI_FN const char *dialogue_line(const char *s,unsigned *length){
 while(*s==' '||*s=='\r')s++;
 unsigned n=0,last=0,width=0;
 while(s[n]&&s[n]!='\n'){
  unsigned left,w=dialogue_glyph((uint8_t)s[n],&left);if(width+w>NPC_TEXT_WIDTH)break;
  if(s[n]==' ')last=n;width+=w+1;n++;
 }
 unsigned used=n;
 if(s[n]&&s[n]!='\n'&&s[n]!=' '&&last)used=last;
 *length=used;s+=used;
 while(*s==' '||*s=='\r')s++;
 if(*s=='\n')s++;
 return s;
}
static UI_FN void dialogue_font(void){
 if(dialogue_font_ready)return;
 for(unsigned i=0;i<16;i++)load_buffer[i]=game.area?0x7f7f7f7f:0;
 dma(load_buffer,(void*)0x06008000,64);
 dialogue_font_ready=1;
}
/* Full names are the retail towners.cpp _tName strings, not abbreviated shop
 * labels. Wounded Townsman is shown only while his contact speech is active. */
static const char *const dialogue_names[TOWN_DIALOGUE_NPC_COUNT]={
 "Griswold the Blacksmith","Ogden the Tavern owner","Wounded Townsman",
 "Adria the Witch","Gillian the Barmaid","Wirt the Peg-legged boy",
 "Pepin the Healer","Cain the Elder","Farnham the Drunk"
};
static const char *const dialogue_talk_labels[TOWN_DIALOGUE_NPC_COUNT]={
 "Talk to Griswold","Talk to Ogden","Talk to townsman","Talk to Adria",
 "Talk to Gillian","Talk to Wirt","Talk to Pepin","Talk to Cain","Talk to Farnham"
};
typedef struct {const char *text;uint16_t length;uint8_t x,y,color,selected;} DialogueLine;
static UI_FN unsigned dialogue_width(const char *text,unsigned length){
 unsigned width=0;for(unsigned i=0;i<length;i++){unsigned left;width+=dialogue_glyph((uint8_t)text[i],&left)+1;}
 return width?width-1:0;
}
static UI_FN void dialogue_center(DialogueLine *lines,unsigned *count,const char *text,unsigned y,unsigned color,unsigned selected){
 unsigned n=0;while(text[n])n++;unsigned width=dialogue_width(text,n);
 lines[(*count)++]=(DialogueLine){text,(uint16_t)n,(uint8_t)((240-width)/2),(uint8_t)y,(uint8_t)color,(uint8_t)selected};
}
/* All menus borrow the same504 hidden terrain tiles; only one1792-byte strip
 * is decoded at a time. Pixel colors and the scrollbar need no extra VRAM or
 * permanent RAM. Map cells are published after the complete screen is ready. */
static inline __attribute__((always_inline)) void dialogue_pixel(uint8_t *pixels,unsigned x,unsigned y,unsigned top,unsigned color){
 if(x<8||x>=232||y<top||y>=top+8)return;
 unsigned px=x-8;pixels[(px>>3)*64+(y-top)*8+(px&7)]=(uint8_t)color;

}
static UI_FN unsigned shop_scroll_position(unsigned *first);
static UI_FN unsigned dialogue_shade(unsigned color,unsigned scale);
static UI_FN void dialogue_scrollbar(uint8_t *pixels,unsigned top,unsigned start,unsigned height,unsigned visible,unsigned total,unsigned first,unsigned border,unsigned edge){
 if(total<=visible||top>=start+height||top+8<=start)return;
 unsigned size=height*visible/total;if(size<8)size=8;
 if(first>total-visible)first=total-visible;
 unsigned thumb=start+(height-size)*first/(total-visible),shadow=info_color(1);
 unsigned best=~0u;for(unsigned i=1;i<256;i++){unsigned c=base_palette[i];int r=(c&31)-6,g=((c>>5)&31)-6,b=((c>>10)&31)-6;unsigned d=r*r+g*g+b*b;if(d<best){best=d;shadow=i;}}
 for(unsigned y=top;y<top+8;y++)if(y>=start&&y<start+height){
  dialogue_pixel(pixels,228,y,top,shadow);dialogue_pixel(pixels,229,y,top,shadow);
  if(y>=thumb&&y<thumb+size){dialogue_pixel(pixels,228,y,top,y==thumb+size-1?edge:border);dialogue_pixel(pixels,229,y,top,y==thumb?border:edge);}
 }
}
/* Cold UI-only shading; the 5x5 information font is unchanged. */
static UI_FN unsigned dialogue_shade(unsigned color,unsigned scale){
 unsigned c=base_palette[color],best=~0u,result=color;
 int r=(c&31)*scale/8,g=((c>>5)&31)*scale/8,b=((c>>10)&31)*scale/8;
 if(r>31)r=31;if(g>31)g=31;if(b>31)b=31;
 for(unsigned i=1;i<256;i++){
  if(!game.area&&!bg_static_index(i))continue;
  c=base_palette[i];int dr=(int)(c&31)-r,dg=(int)((c>>5)&31)-g,db=(int)((c>>10)&31)-b;
  unsigned d=dr*dr+dg*dg+db*db;if(d<best){best=d;result=i;}
 }
 return result;
}
/* Cold dialogue composition uses Thumb to keep the 32 MiB ROM budget. */
static UI_FN __attribute__((target("thumb"),minsize)) void dialogue_canvas(const DialogueLine *lines,unsigned count){
 uint8_t *pixels=(uint8_t*)load_buffer;
 unsigned inks[5];for(unsigned c=0;c<5;c++)inks[c]=hud_color_index(c);
 unsigned shadow=info_color(1);
 unsigned best=~0u;for(unsigned i=1;i<256;i++){unsigned c=base_palette[i];int r=(c&31)-6,g=((c>>5)&31)-6,b=((c>>10)&31)-6;unsigned d=r*r+g*g+b*b;if(d<best){best=d;shadow=i;}}
 /* Sample the actual frame tile's highlight and edge palette entries. */
 const volatile uint8_t *frame=(const volatile uint8_t*)0x0600b080;
 unsigned scroll_border=frame[12],scroll_edge=frame[20];
 /* Town and dungeon both reserve palette 208 for the same retail speech
  * ivory. Quest Read uses the identical prose color in either location. */
 unsigned light[6],dark[6];for(unsigned i=0;i<6;i++){unsigned ink=i==5?NPC_PROSE_COLOR:inks[i];light[i]=dialogue_shade(ink,10);dark[i]=dialogue_shade(ink,5);}
 unsigned scroll_first=dialogue_row,scroll_total=dialogue_lines,scroll_rows=NPC_ROWS,scroll_height=100;
 if(menu_mode==7||menu_mode==8){scroll_total=shop_scroll_position(&scroll_first);scroll_rows=menu_mode==7?4:5;scroll_height=menu_mode==7?82:64;}
 else if(menu_mode!=NPC_SPEECH)scroll_total=0;
 for(unsigned row=1;row<=18;row++){
  unsigned used=0,top=row*8;for(unsigned i=0;i<NPC_WIDTH*8/4;i++)load_buffer[i]=game.area?0x7f7f7f7f:0;
  for(unsigned n=0;n<count;n++){
   const DialogueLine *line=&lines[n];
   if(line->y+8<=top||line->y>=top+8)continue;
   unsigned x=line->x,color=line->color==NPC_PROSE_COLOR?NPC_PROSE_COLOR:inks[line->color];
   for(unsigned c=0;c<line->length;c++){
    unsigned ch=(uint8_t)line->text[c],left,width=dialogue_glyph(ch,&left);if(ch<32||ch>=128)ch='?';
    if(x+width>232)break;
    unsigned shade=line->color==NPC_PROSE_COLOR?5:line->color;
    for(unsigned pass=0;pass<2;pass++)for(unsigned gy=0;gy<TOWN_DIALOGUE_FONT_INK_HEIGHT;gy++){
     unsigned y=line->y+gy+(pass?0:1);if(y<top||y>=top+8)continue;
     unsigned bits=town_dialogue_font[ch-32][gy];
     for(unsigned gx=0;gx<width;gx++)if(bits&(128u>>(left+gx))){
      unsigned bit=128u>>(left+gx),above=gy?town_dialogue_font[ch-32][gy-1]:0,below=town_dialogue_font[ch-32][gy+1];
      unsigned ink=!pass?shadow:!(above&bit)?light[shade]:!(below&bit)?dark[shade]:color;
      dialogue_pixel(pixels,x+gx+(pass?0:1),y,top,ink);
     }
    }
    x+=width+1;
   }
   if(line->selected){
    unsigned left=line->x-13,right=x+4;
    /* PrintSString uses the same PentSpn2 frame on both sides. Frame0 is the
     * face-on retail symbol; input redraws do not need continuous animation. */
    for(unsigned y=0;y<NPC_MENU_CURSOR_HEIGHT;y++)for(unsigned x=0;x<NPC_MENU_CURSOR_WIDTH;x++)if(npc_menu_cursor[0][y*NPC_MENU_CURSOR_WIDTH+x]){
     unsigned color=npc_menu_cursor[0][y*NPC_MENU_CURSOR_WIDTH+x];
     dialogue_pixel(pixels,left+x,line->y+y,top,color);dialogue_pixel(pixels,right+x,line->y+y,top,color);
    }
   }
   used=1;
  }
  if(scroll_total>scroll_rows&&top<32+scroll_height&&top+8>32){
   dialogue_scrollbar(pixels,top,32,scroll_height,scroll_rows,scroll_total,scroll_first,scroll_border,scroll_edge);
   used=1;
  }
  if(used){for(unsigned col=1;col<=NPC_COLUMNS;col++)hud_map[row*32+col]=(uint16_t)((row-1)*NPC_COLUMNS+col-1);
   dma(pixels,(void*)(0x06000000+(row-1)*NPC_WIDTH*8),NPC_WIDTH*8);}
 }
}
static UI_FN void dialogue_restore_bg(void){
 if(R16(0x0400000a)&8)return;
 /* Clear old canvas references before returning to ordinary menu fonts. */
 for(unsigned i=0;i<640;i++)R16(0x0600d800+i*2)=128;
 R16(0x0400000a)|=8;
}
static UI_FN void dialogue_end(void){
 if(!dialogue_session)return;
 audio_speech_stop();
 /* Prose glyphs borrow terrain VRAM; restore it before revealing the town. */
 R16(0x04000000)=0;
 dialogue_restore_bg();
 for(unsigned i=0;i<640;i++)hud_map[i]=128;
 for(unsigned i=0;i<NPC_CANVAS_TILES;i++)terrain_invalidate(i);
 terrain_invalidate(512);
 if(game.area)stream_town();
 dialogue_session=dialogue_dirty=dialogue_font_ready=0;
}
static UI_FN void dialogue_voice(unsigned message){
 audio_speech_stop();
 if(message<TOWN_DIALOGUE_MESSAGE_COUNT&&town_dialogue_messages[message].voice!=TOWN_DIALOGUE_NONE)
  audio_speech_start(town_dialogue_messages[message].voice,0,0);
}
static UI_FN void dialogue_say(unsigned message,unsigned return_mode){
 if(message<TOWN_DIALOGUE_MESSAGE_COUNT&&!town_dialogue_messages[message].text_offset)return;
 if(message>=TOWN_DIALOGUE_MESSAGE_COUNT&&(!dialogue_title(message)||!*dialogue_title(message)))return;
 dialogue_message=message;dialogue_return=(uint8_t)return_mode;dialogue_row=0;dialogue_lines=0;
 dialogue_repeat_key=dialogue_repeat_fast=0;
 const char *s=dialogue_text(message);while(*s){unsigned n;s=dialogue_line(s,&n);dialogue_lines++;}
 menu_mode=NPC_SPEECH;dialogue_dirty=1;audio_speech_stop();
}
static UI_FN int dialogue_topic(unsigned quest){
 if(quest==6)return butcher_quest==2;
 if(quest==5)return quest_progress.stage[15]>=8&&quest_progress.state[5]==2;
 return quest_progress.state[quest]==2;
}
static UI_FN int dialogue_topic_message(unsigned quest){
 int message=town_dialogue_topics[dialogue_npc][quest];
 if(quest==1){
  if(dialogue_npc==6&&quest_progress.stage[1]>=4&&quest_progress.stage[1]<6)message=TOWN_TEXT_MUSH3;
  if(dialogue_npc==3&&quest_progress.stage[1]>=4)message=-1;
 }
 return message;
}
/* Row zero is Gossip; the last row is Back. No saved or shared state changes. */
static UI_FN unsigned dialogue_topic_count(void){
 unsigned n=2;for(unsigned q=0;q<16;q++)if(dialogue_topic(q)&&dialogue_topic_message(q)>=0)n++;return n;
}
static UI_FN int dialogue_topic_at(unsigned row){
 for(unsigned q=0;q<16;q++)if(dialogue_topic(q)&&dialogue_topic_message(q)>=0){if(!row--)return (int)q;}return -1;
}
static UI_FN void dialogue_topics_open(unsigned npc){
 audio_speech_stop();dialogue_npc=(uint8_t)npc;dialogue_choice=0;menu_mode=NPC_TOPICS;dialogue_dirty=1;
}
#include "shop_ui.inc"
static UI_FN void dialogue_open(unsigned npc,unsigned notice,unsigned wounded_first){
 dialogue_npc=(uint8_t)npc;dialogue_choice=0;dialogue_session=1;dialogue_dirty=1;dialogue_font_ready=0;
 dialogue_gossip=(uint8_t)graphics_frames;
 if(!shop_open_npc(npc))menu_mode=NPC_LANDING;
 if(wounded_first){dialogue_say(TOWN_TEXT_BUTCH9,0);return;}
 if(notice){
  if(notice<TOWN_DIALOGUE_NOTICE_COUNT&&town_dialogue_notice[notice]!=TOWN_DIALOGUE_NONE)dialogue_say(town_dialogue_notice[notice],menu_mode);
  else{quest_notice=notice;dialogue_return=(uint8_t)menu_mode;menu_mode=11;}
  dialogue_voice(town_dialogue_npcs[npc].greeting);
  return;
 }
 /* The dying townsman has no shop/gossip in the original game. */
 if(npc==2){dialogue_say(TOWN_TEXT_BUTCH9,0);return;}
 if(npc==1&&!quest_progress.visited)dialogue_say(TOWN_TEXT_INTRO,menu_mode);
 if(npc==3&&quest_progress.state[1]==2&&quest_progress.stage[1]==5){
  const Inventory *bag=game_inventory();
  for(unsigned i=0;i<ITEM_SLOTS;i++)if(bag->items[i].kind==ITEM_BRAIN){dialogue_say(TOWN_TEXT_MUSH11,menu_mode);break;}
 }
 dialogue_voice(town_dialogue_npcs[npc].greeting);
}
static UI_FN unsigned dialogue_repeat(unsigned pressed){
 unsigned held=(~R16(0x04000130))&(KEY_UP|KEY_DOWN);uint16_t now=audio_game_clock();
 if(!held){dialogue_repeat_key=0;return pressed;}
 if((pressed&held)||held!=dialogue_repeat_key){dialogue_repeat_key=(uint8_t)held;dialogue_repeat_clock=now;dialogue_repeat_fast=0;return pressed;}
 if((uint16_t)(now-dialogue_repeat_clock)>=(dialogue_repeat_fast?1638:5734)){
  dialogue_repeat_clock=now;dialogue_repeat_fast=1;pressed|=held;
 }
 return pressed;
}
static UI_FN void dialogue_draw(void){
 dialogue_font();
 /* The staged map borrows the fourth decoded town-actor cache buffer. */
 for(unsigned i=0;i<4;i++)raw_base[i]=0;
 /* Canvas borrows hidden world tiles0..503 plus one black tile512. */
 lighting_hardware_stop();map_last_x=-1;last_hud_tick=~0u;
 volatile uint16_t *screen=hud_map;hud_map=(volatile uint16_t*)((uint8_t*)load_buffer+8192);
 DialogueLine lines[NPC_ROWS+4];unsigned line_count=0;char shop_text[26][64];
 unpack_to(game.area?asset_town_status_frame:asset_dungeon_status_frame,(void*)0x0600b040,9*64);
 for(unsigned i=0;i<640;i++)hud_map[i]=0;
 for(int y=0;y<20;y++)for(int x=0;x<30;x++){
  if(!x||x==29||!y||y==19)hud_map[y*32+x]=(uint16_t)(193+(y==0?0:y==19?2:1)*3+(x==0?0:x==29?2:1));
 }
 dialogue_center(lines,&line_count,(menu_mode==NPC_SPEECH&&dialogue_message>=TOWN_DIALOGUE_MESSAGE_COUNT)?dialogue_title(dialogue_message):dialogue_names[dialogue_npc],10,HUD_GOLD,0);
 for(unsigned x=1;x<29;x++)hud_map[3*32+x]=194;
 if(menu_mode==11){
  /* Local reward/reminder notices have no retail speech entry. They retain
   * their existing meanings and A/B return path inside the same NPC frame. */
  const char *message=quest_notice==23?"Bring his staff to Cain":quest_notice==24?"You are too late":quest_notice==30?"Find the Fungal Tome":"No room for the reward";
  dialogue_center(lines,&line_count,message,68,HUD_WHITE,0);
  if(quest_notice==23||quest_notice==24||quest_notice==3||quest_notice==6||quest_notice>=29)
   dialogue_center(lines,&line_count,quest_notice==23?"Search level 15":quest_notice==24?"Defeat Lazarus":"Return when you have it",88,HUD_WHITE,0);
  dialogue_center(lines,&line_count,"B: Back",140,HUD_WHITE,0);
 }else if(menu_mode==NPC_SPEECH){
  const char *s=dialogue_text(dialogue_message);
  for(unsigned row=0;row<dialogue_row&&*s;row++){unsigned count;s=dialogue_line(s,&count);}
  for(unsigned row=0;row<NPC_ROWS&&*s;row++){
   while(*s==' '||*s=='\r')s++;unsigned count;const char *next=dialogue_line(s,&count);
   lines[line_count++]=(DialogueLine){s,(uint16_t)count,8,(uint8_t)(32+row*NPC_LINE_STEP),NPC_PROSE_COLOR,0};s=next;
  }
  dialogue_center(lines,&line_count,"B: Back",140,HUD_WHITE,0);
 }else if(menu_mode==NPC_TOPICS){
  unsigned count=dialogue_topic_count();if(dialogue_choice>=count)dialogue_choice=(uint8_t)(count-1);
  /* Back is always at the bottom, including on additional topic pages. */
  unsigned first=(dialogue_choice+1==count?0:dialogue_choice)/6*6;
  dialogue_center(lines,&line_count,"Would you like to:",36,HUD_GOLD,0);
  for(unsigned i=first;i+1<count&&i<first+6;i++){
   int quest=i?dialogue_topic_at(i-1):-1;
   dialogue_center(lines,&line_count,i?town_dialogue_quest_names[quest]:"Gossip",50+(i-first)*14,i?HUD_WHITE:HUD_BLUE,dialogue_choice==i);
  }
  dialogue_center(lines,&line_count,"B: Back",140,HUD_WHITE,dialogue_choice+1==count);
 }else if(menu_mode>=7&&menu_mode<=10){
  shop_dialogue(lines,&line_count,shop_text);
 }else{
  dialogue_center(lines,&line_count,"Would you like to:",36,HUD_GOLD,0);
  dialogue_center(lines,&line_count,dialogue_talk_labels[dialogue_npc],76,HUD_BLUE,dialogue_choice==0);
  dialogue_center(lines,&line_count,"B: Say goodbye",140,HUD_WHITE,dialogue_choice==1);
 }
 /* Ordinary frame/HUD tiles still reside at8000+, now addressed frombase0. */
 for(unsigned i=0;i<640;i++)hud_map[i]+=512;
 dialogue_canvas(lines,line_count);
 unsigned scan;do{scan=R16(0x04000006);}while(scan<160||scan>220);
 dma((const void*)hud_map,(void*)screen,1280);hud_map=screen;
 R16(0x0400000a)&=(uint16_t)~12u;
 R16(0x04000000)=0x0240;
 dialogue_dirty=0;
}
static UI_FN void dialogue_input(unsigned pressed){
 if(pressed&KEY_START){menu_close();return;}
 if(menu_mode==NPC_SPEECH){
  if(pressed&(KEY_A|KEY_B)){
   audio_speech_stop();if(dialogue_return==NPC_QUEST_RETURN){questlog_return();return;}if(!dialogue_return){menu_close();return;}menu_mode=dialogue_return;
  }else if(pressed&(KEY_UP|KEY_DOWN)){
   if((pressed&KEY_UP)&&dialogue_row)dialogue_row--;
   if((pressed&KEY_DOWN)&&dialogue_row+NPC_ROWS<dialogue_lines)dialogue_row++;
  }
 }else if(menu_mode==NPC_TOPICS){
  unsigned count=dialogue_topic_count();
  if(pressed&KEY_UP)dialogue_choice=(uint8_t)((dialogue_choice+count-1)%count);
  if(pressed&KEY_DOWN)dialogue_choice=(uint8_t)((dialogue_choice+1)%count);
  if((pressed&KEY_B)||((pressed&KEY_A)&&dialogue_choice+1==count)){
   menu_mode=shop_open_npc(dialogue_npc)?10:NPC_LANDING;dialogue_choice=0;
  }else if(pressed&KEY_A){
   if(!dialogue_choice){
    const TownDialogueNpc *npc=&town_dialogue_npcs[dialogue_npc];
    if(npc->gossip_count)dialogue_say(town_dialogue_gossip[npc->gossip_first+(dialogue_gossip++%npc->gossip_count)],NPC_TOPICS);
   }else{int q=dialogue_topic_at(dialogue_choice-1);if(q>=0)dialogue_say((unsigned)dialogue_topic_message((unsigned)q),NPC_TOPICS);}
  }
 }else{
  if(pressed&KEY_B){menu_close();return;}
  if(pressed&(KEY_UP|KEY_DOWN))dialogue_choice^=1;
  if(pressed&KEY_A){if(dialogue_choice){menu_close();return;}dialogue_topics_open(dialogue_npc);}
 }
 dialogue_dirty=1;menu_draw();
}
