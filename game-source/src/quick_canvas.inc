/* Paused 7x7 / 3x5 UI. Build strips in unused OBJ VRAM, then publish the
 * completed BG canvas in VBlank. No permanent framebuffer allocation. */
static uint8_t spellbook_page,spellbook_row;
#include "../generated/spellbook_palette.h"
static const uint8_t spellbook_pages[4][7]={
 {SPELL_SKILL,SPELL_FIREBOLT,SPELL_CHARGED,SPELL_HOLY,SPELL_HEAL,SPELL_HEAL_OTHER,SPELL_INFERNO},
 {SPELL_RESURRECT,SPELL_FIREWALL,SPELL_TELEKINESIS,SPELL_LIGHTNING,SPELL_TOWN,SPELL_FLASH,SPELL_STONE},
 {SPELL_PHASING,SPELL_SHIELD,SPELL_ELEMENTAL,SPELL_FIREBALL,SPELL_WAVE,SPELL_CHAIN,SPELL_GUARDIAN},
 {SPELL_NOVA,SPELL_GOLEM,SPELL_TELEPORT,SPELL_APOCALYPSE,SPELL_BONE_SPIRIT,SPELL_BLOOD_STAR,0}
};
/* Devilution control.cpp SpellPages / DrawSpellBook: class skill, learned
 * spells and equipped staff charges. Scrolls remain in the quick-slot picker. */
static UI_FN unsigned spellbook_choice(unsigned spell){
 if(spell==SPELL_SKILL)return spell;
 if(!spell)return 0;
 int staff=game_staff_slot();
 if(staff>=0){const Item *it=&inventory_view()->items[staff];if(item_spell(it)==spell&&it->charges)return SPELL_STAFF;}
 return game_spell_level(&hero_progress,spell)?spell:0;
}
static UI_FN unsigned quick_ink(unsigned color){
 unsigned best=~0u,at=0;
 for(unsigned i=0;i<256;i++){
  if(!game.area&&i&&!bg_static_index(i))continue;
  unsigned c=base_palette[i];int r=(int)(c&31)-(int)(color&31),g=(int)((c>>5)&31)-(int)((color>>5)&31),b=(int)(c>>10)-(int)(color>>10);
  unsigned d=r*r+g*g+b*b;if(d<best){best=d;at=i;}
 }
 return at;
}
static UI_FN unsigned quick_tiny_bits(unsigned ch,unsigned y){
 if(ch>='a'&&ch<='z')ch-=32;
 if(ch=='(')return y==0||y==4?1:2;
 if(ch==')')return y==0||y==4?4:2;
 if(ch=='-')return y==2?7:0;
 if(ch=='/')return y<2?1:y==2?2:4;
 if(ch=='.')return y==4?2:0;
 if(ch==',')return y==3?2:y==4?4:0;
 if(ch=='?'){static const uint8_t question[5]={6,1,2,0,2};return question[y];}
 if(ch=='%'){static const uint8_t percent[5]={5,1,2,4,5};return percent[y];}
 unsigned i=ch>='A'&&ch<='Z'?ch-'A'+12:ch>='0'&&ch<='9'?ch-'0'+1:ch==':'?11:0;
 return overlay_rows[i][y];
}
static UI_FN unsigned quick_text_width(const char *text,unsigned tiny){
 unsigned n=0;while(text[n])n++;
 return tiny?(n?n*(tiny==2?6:4)-1:0):dialogue_width(text,n);
}
/* Small palette cache is shared by all paused panels, never the frame loop. */
static uint8_t quick_shades[4][3];
static UI_FN void quick_style(void){
 for(unsigned i=0;i<4;i++){unsigned ink=i==0?hud_color_index(HUD_WHITE):i==1?hud_color_index(HUD_GOLD):i==3?hud_color_index(HUD_MENU):quick_ink(12|(12<<5)|(12<<10));quick_shades[i][0]=ink;quick_shades[i][1]=dialogue_shade(ink,10);quick_shades[i][2]=dialogue_shade(ink,5);}
}
static UI_FN void quick_text(uint8_t *pixels,unsigned top,int x,unsigned y,const char *text,unsigned color,unsigned tiny){
 if(y+(tiny?6u:8u)<=top||y>=top+8)return;
 unsigned light=color,dark=color;for(unsigned i=0;i<4;i++)if(quick_shades[i][0]==color){light=quick_shades[i][1];dark=quick_shades[i][2];break;}
 for(;*text;text++){
  unsigned ch=(uint8_t)*text,left=0,width=tiny?(tiny==2?5:3):dialogue_glyph(ch,&left);
  if(ch<32||ch>=128)ch='?';
  uint8_t glyph[9]={0};unsigned height=tiny?5:7;
  for(unsigned gy=0;gy<height;gy++)glyph[gy+1]=(uint8_t)(tiny==2?info_font_rows[ch][gy]:tiny?quick_tiny_bits(ch,gy):town_dialogue_font[ch-32][gy]);
  for(unsigned pass=0;pass<2;pass++)for(unsigned gy=0;gy<height;gy++){
   unsigned py=y+gy+!pass;if(py<top||py>=top+8)continue;
   unsigned bits=glyph[gy+1],above=glyph[gy],below=glyph[gy+2];
   for(unsigned gx=0;gx<width;gx++){unsigned bit=(tiny==1?4u:128u)>>(left+gx);if(bits&bit)dialogue_pixel(pixels,x+gx+!pass,py,top,!pass?0:!(above&bit)?light:!(below&bit)?dark:color);}
  }
  x+=(int)width+1;
 }
}
static UI_FN void quick_label(uint8_t *pixels,unsigned top,unsigned center,unsigned y,const char *text,const char *key,unsigned ink,unsigned dim){
 if(y+7<=top||y>=top+8)return;
 unsigned a=quick_text_width(text,0),b=quick_text_width(key,1);int x=(int)center-(int)(a+4+b)/2;
 quick_text(pixels,top,x,y,text,ink,0);quick_text(pixels,top,x+a+4,y+1,key,dim,1);
}
/* The inventory's left120px have their own240 canvas tiles after the83
 * information-font tiles. The right MPQ artwork and bottom item text stay
 * separate. Every visible help glyph is the approved7x7 font. */
static UI_FN void inventory_controls(void);
static UI_FN void quick_rect(uint8_t *pixels,unsigned top,unsigned x,unsigned y,unsigned w,unsigned h,unsigned color,unsigned outline){
 unsigned start=y>top?y:top,end=y+h<top+8?y+h:top+8;
 for(unsigned py=start;py<end;py++)for(unsigned px=x;px<x+w;px++)if(!outline||px==x||px==x+w-1||py==y||py==y+h-1)dialogue_pixel(pixels,px,py,top,color);
}
#include "../generated/ui128_assets.h"
/* One native border shared by both panels. Scratch is disjoint from the
 * current 1792-byte strip and inventory's staged map at offset 17408. */
static UI_FN void inventory_frame_prepare(void){
 uint8_t lut[UI128_COLORS],*border=(uint8_t*)load_buffer+4096;
 for(unsigned i=0;i<UI128_COLORS;i++)lut[i]=quick_ink(ui128_palette[i]);
 lz_decode(asset_ui128_frames,border,1452);
 for(unsigned i=0;i<1452;i++)border[i]=lut[border[i]];
}
static UI_FN void inventory_frame_strip(uint8_t *p,unsigned top,unsigned y,unsigned w,unsigned h){
 const uint8_t *border=(const uint8_t*)load_buffer+4096;
 for(unsigned py=top;py<top+8;py++){
  if(py<y||py>=y+h)continue;
  unsigned dy=py-y,sy=dy<3?dy:dy>=h-3?128-(h-dy):3+(dy-3)%122;
  for(unsigned x=0;x<w;x++)if(x<3||x>=w-3||dy<3||dy>=h-3){
   unsigned sx=x<3?x:x>=w-3?120-(w-x):3+(x-3)%114;
   unsigned at=sy<3?sy*120+sx:sy>=125?360+(sy-125)*120+sx:720+(sy-3)*6+(sx<3?sx:sx-114);
   dialogue_pixel(p,8+x,py,top,border[at]);
  }
 }
}
static UI_FN char *quick_number(char *p,unsigned n){return p+info_uint(p,n);}
static UI_FN char *quick_append(char *p,const char *s){while(*s)*p++=*s++;return p;}
static UI_FN unsigned spellbook_scale(unsigned n,unsigned level){while(level--)n+=n>>3;return n>65535?65535:n;}
/* Endpoints of this port's damage_roll, without consuming its random state. */
static UI_FN int spellbook_damage(unsigned s,unsigned level,unsigned *lo,unsigned *hi){
 unsigned pl=hero_progress.level,mag=game_effective_stat(1),a=0,b=0;
 switch(s){
 case SPELL_FIREBOLT:a=(mag>>3)+level+1;b=a+9;break;
 case SPELL_HOLY:a=pl+9;b=a+9;break;
 case SPELL_FIREBALL:case SPELL_ELEMENTAL:a=spellbook_scale(2*pl+4,level);b=spellbook_scale(2*(pl+18)+4,level);if(s==SPELL_ELEMENTAL){a/=2;b/=2;}break;
 case SPELL_BLOOD_STAR:a=b=mag/2+3*level-mag/8;break;
 case SPELL_LIGHTNING:case SPELL_CHAIN:a=2;b=pl?pl*2:2;break;
 case SPELL_CHARGED:a=1;b=mag>>2;if(!b)b=1;break;
 case SPELL_NOVA:a=spellbook_scale((pl+5)/2,level);b=spellbook_scale((pl+30)/2,level);break;
 case SPELL_GUARDIAN:a=spellbook_scale((pl>>1)+1,level);b=spellbook_scale((pl>>1)+10,level);break;
 case SPELL_FIREWALL:a=pl+2;b=pl+20;break;
 case SPELL_INFERNO:a=3;b=3*((pl?pl-1:0)+3)/2;break;
 case SPELL_WAVE:a=pl+1;b=pl+10;break;
 case SPELL_APOCALYPSE:a=pl;b=pl*6;break;
 case SPELL_FLASH:a=spellbook_scale(pl+1,level)*3/2;b=spellbook_scale((pl+1)*20,level)*3/2;break;
 default:return 0;
 }
 *lo=a;*hi=b;return 1;
}
static UI_FN __attribute__((minsize)) void spellbook_detail(unsigned choice,char text[64]){
 char *p=text;
 if(choice==SPELL_SKILL)p=quick_append(p,"Skill");
 else if(choice==SPELL_STAFF){int i=game_staff_slot();p=quick_append(p,"Staff (");p=quick_number(p,i>=0?inventory_view()->items[i].charges:0);p=quick_append(p," charges)");}
 else{
  unsigned level=game_spell_effective_level(choice),lo,hi;
  p=quick_append(p,"Lv ");p=quick_number(p,level);p=quick_append(p,"  Mana ");p=quick_number(p,game_spell_cost(choice)>>6);
  p=quick_append(p,"  Dmg ");
  if(choice==SPELL_BONE_SPIRIT)p=quick_append(p,"1/3 HP");
  else if(spellbook_damage(choice,level,&lo,&hi)){p=quick_number(p,lo);*p++='-';p=quick_number(p,hi);}
  else p=quick_append(p,"--");
 }
 *p=0;
}
static UI_FN __attribute__((target("thumb"),minsize)) void quick_canvas_draw(unsigned book){
 if(quick_painted==quick_mode&&quick_painted_notice==quick_notice)return;
 lighting_hardware_stop();map_pending=0;hud_font_mode(0);quick_style();
 unsigned white=hud_color_index(HUD_WHITE),gold=hud_color_index(HUD_GOLD),gray=quick_ink(12|(12<<5)|(12<<10)),edge=quick_ink(6|(5<<5)|(3<<10)),action=quick_shades[3][0];
 uint8_t choices[7]={0};const uint8_t (*lut)[16]=spellbook_icon_lut[!game.area];char details[7][64];
 if(book==1){
  inventory_frame_prepare();
  for(unsigned i=0;i<7;i++){choices[i]=(uint8_t)spellbook_choice(spellbook_pages[spellbook_page][i]);if(choices[i])spellbook_detail(choices[i],details[i]);}
 }
 for(unsigned i=0;i<128;i++)R16(0x07000000+i*8)=0x0200;
 /* The displayed canvas stays intact while all new strips are composed. */
 for(unsigned row=1;row<=18;row++){
  unsigned top=row*8;uint8_t *pixels=(uint8_t*)load_buffer;
  for(unsigned i=0;i<NPC_WIDTH*8/4;i++)load_buffer[i]=0;
  if(book==1)inventory_frame_strip(pixels,top,8,224,144);
  else{quick_rect(pixels,top,8,8,224,144,gold,1);quick_rect(pixels,top,9,9,222,142,edge,1);}
  if(!book){
   const char *title="Shortcuts";quick_text(pixels,top,120-quick_text_width(title,0)/2,19,title,gold,0);
   quick_label(pixels,top,58,43,"Quest","(L)",action,gray);
   quick_label(pixels,top,184,43,"SPELL","(R)",action,gray);
   quick_label(pixels,top,120,63,"MENU","(up)",action,gray);
   quick_label(pixels,top,66,83,"Character","(left)",action,gray);
   quick_label(pixels,top,182,83,"Inventory","(Right)",action,gray);
   quick_label(pixels,top,120,103,"MAP","(down)",action,gray);
   quick_label(pixels,top,66,123,"Quick spell","(Select)",action,gray);
  }else{
   quick_text(pixels,top,16,12,"Spells",gold,0);
   quick_text(pixels,top,60,13,"B: Back",white,1);
   quick_text(pixels,top,226-quick_text_width("A: Assign to current quick slot",1),13,"A: Assign to current quick slot",white,1);
   for(unsigned i=0;i<7;i++){
    unsigned y=23+i*16;if(y+16<=top||y>=top+8)continue;quick_rect(pixels,top,16,y,16,16,gray,1);
    unsigned choice=choices[i];if(!choice)continue;
    unsigned key=game_spell_icon_key(&game,choice),type=key>>8;if(type>4)type=4;
    const uint8_t *icon=(const uint8_t*)spell_flat_icons[key&255];
    for(unsigned yy=0;yy<14;yy++)for(unsigned xx=0;xx<14;xx++){
     unsigned sx=xx*24/14,sy=yy*24/14,at=(sy/8)*96+(sx/8)*32+(sy&7)*4+(sx&7)/2,c=(icon[at]>>((sx&1)*4))&15;
     if(c)dialogue_pixel(pixels,17+xx,y+1+yy,top,lut[type][c]);
    }
    if(i==spellbook_row)quick_rect(pixels,top,15,y,215,16,gold,1);
    static const char *skills[]={"Item Repair","Trap Disarm","Staff Recharge"};
    quick_text(pixels,top,37,y+1,choice==SPELL_SKILL?skills[game_hero_class()]:game_spell_name(choice),type==SPELL_ICON_INVALID?gray:action,0);
    quick_text(pixels,top,37,y+9,details[i],white,1);
   }
   for(unsigned i=0;i<4;i++){
    unsigned x=16+i*53;quick_rect(pixels,top,x,137,51,12,edge,0);quick_rect(pixels,top,x,137,51,12,i==spellbook_page?gold:gray,1);
    char n[2]={(char)('1'+i),0};quick_text(pixels,top,x+23,140,n,action,0);
   }
  }
  dma(pixels,(void*)(0x06010000+(row-1)*NPC_WIDTH*8),NPC_WIDTH*8);
 }
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 dma((const void*)0x06010000,(void*)0x06000000,NPC_CANVAS_TILES*64);
 for(unsigned i=0;i<640;i++)hud_map[i]=640;
 for(unsigned row=1;row<=18;row++)for(unsigned col=1;col<=28;col++)hud_map[row*32+col]=(uint16_t)((row-1)*28+col-1);
 R16(0x0400000a)=0x1b80;R16(0x04000000)=0x0240;R16(0x04000050)=0;
 inventory_art_active=2;quick_painted=quick_mode;quick_painted_notice=quick_notice;
}
static UI_FN void spellbook_input(unsigned pressed){
 if(!pressed)return;
 if(pressed&KEY_B){quick_mode=1;quick_notice=0;quick_clear();return;}
 if(pressed&(KEY_L|KEY_R|KEY_LEFT|KEY_RIGHT)){
  spellbook_page=(spellbook_page+((pressed&(KEY_L|KEY_LEFT))?3:1))&3;spellbook_row=0;
  for(unsigned i=0;i<7;i++)if(spellbook_choice(spellbook_pages[spellbook_page][i])){spellbook_row=(uint8_t)i;break;}
 }else if(pressed&(KEY_UP|KEY_DOWN)){
  unsigned row=spellbook_row;
  for(unsigned i=0;i<7;i++){row=(row+((pressed&KEY_UP)?6:1))%7;if(spellbook_choice(spellbook_pages[spellbook_page][row])){spellbook_row=(uint8_t)row;break;}}
 }else if(pressed&KEY_A){
  unsigned choice=spellbook_choice(spellbook_pages[spellbook_page][spellbook_row]);
  if(choice&&game_spell_available(choice)){
   if(coop_active)inventory_command=game_spell_wire(2,hero_progress.spell_active_slot,choice);
   else game_spell_bind_slot(hero_progress.spell_active_slot,choice);
   quick_notice=2;quick_painted=0;return;
  }
 }
 quick_notice=0;quick_painted=0;
}
