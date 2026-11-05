/* Native MPQ frontend. Mode4 double buffering; scene RAM is reused only while
 * gameplay is suspended. Startup sequence follows Diablo diablo.cpp/DiabloUI. */
#include "link.h"
#include "../generated/frontend.h"
#include "../generated/front_opening.h"
#include "../generated/front_opening_audio.h"
#include "../generated/frontend_font.h"
#include "front_delta.inc"
_Static_assert(FRONT_OPENING_FRAME_BYTES+FRONT_OPENING_SCRATCH_BYTES<=sizeof(town_map),"opening scratch exceeds suspended scene arena");
volatile unsigned frontend_mode,frontend_frame,link_map_visible;
static UI_FN void front_vblank(void){audio_update();while(R16(0x04000006)>=160){link_poll();}while(R16(0x04000006)<160){link_poll();}link_refresh((~R16(0x04000130))&1023);}
static void front_sound_stop(void){R32(0x040000c4)=0;R16(0x04000106)=0;R16(0x04000084)=0;}
static UI_FN void front_sound_start(void){
 front_sound_stop();R16(0x04000084)=0x80;R16(0x04000080)=0;R16(0x04000082)=0x0f04;
 R32(0x040000bc)=(uint32_t)asset_front_audio;R32(0x040000c0)=0x040000a0;R32(0x040000c4)=0xb6400004;
 R16(0x04000104)=FRONT_OPENING_AUDIO_TIMER_RELOAD;R16(0x04000106)=0x80;
}
static UI_FN void front_palette(const uint8_t *p){lz_decode(p,base_palette,512);dma(base_palette,(void*)0x05000000,512);}
static UI_FN void front_present(unsigned *page){
 lighting_hardware_stop();
 unsigned next=*page^1;dma(town_map,(void*)(0x06000000+next*0xa000),38400);
 front_vblank();R16(0x04000050)=0;R16(0x04000054)=0;R16(0x04000000)=0x0404|(next<<4);*page=next;
}
/* A local palette selects direct Mode3 output without touching live scene RAM. */
static UI_FN void front_draw_text(int center,int y,const char *text,unsigned color,const uint16_t *palette){
 int width=0;
 for(int n=0;text[n];n++){int i=0;while(glyphs[i]&&glyphs[i]!=text[n])i++;if(glyphs[i])width+=asset_front_font_widths[i];}
 int x=center-width/2;uint8_t *dst=(uint8_t*)town_map;
 for(int n=0;text[n];n++){
  int i=0;while(glyphs[i]&&glyphs[i]!=text[n])i++;if(!glyphs[i])continue;
  const uint8_t *glyph=asset_front_font+i*FRONT_FONT_STRIDE*FRONT_FONT_HEIGHT+(color==254?FRONT_FONT_BANK:0);
  for(int yy=0;yy<FRONT_FONT_HEIGHT;yy++)for(int xx=0;xx<FRONT_FONT_STRIDE;xx++){
   unsigned pixel=glyph[yy*FRONT_FONT_STRIDE+xx];int px=x+xx,py=y+yy;
   if(pixel&&px>=0&&px<240&&py>=0&&py<160){
    if(palette)R16(0x06000000+2*(py*240+px))=palette[color==255?255:pixel];
    else dst[py*240+px]=color==255?255:pixel;
   }
  }
  x+=asset_front_font_widths[i];
 }
}
static UI_FN void front_text_at(int center,int y,const char *text,unsigned color){front_draw_text(center,y,text,color,0);}
static UI_FN void front_exit_notice(void){
 uint16_t palette[256];
 lz_decode(asset_front_palette,palette,sizeof(palette));
 front_draw_text(120,76,"PLEASE WAIT",254,palette);
}
static void front_text(int y,const char *text,unsigned color){front_text_at(120,y,text,color);}
/* Exact bevel rule and palette indices from all 40 native 7x7 frontend
 * glyphs: exposed upper edge=2, body=10, lower edge=42, shadow=91. */
static inline __attribute__((always_inline)) unsigned front_small_ink(unsigned above,unsigned below,unsigned bit){
 return !(above&bit)?2:!(below&bit)?42:10;
}
/* The gameplay 5x5 font includes punctuation absent from the title font. */
static UI_FN __attribute__((target("thumb"),minsize)) void front_info_text(int y,const char *text){
 unsigned width=0;uint8_t *pixels=(uint8_t*)town_map;
 for(const char *s=text;*s;s++){unsigned left;width+=info_glyph_width((uint8_t)*s,&left)+1;}
 if(width)width--;
 /* Draw every shadow before any face so adjacent letters stay legible. */
 for(unsigned pass=0;pass<2;pass++){
  int x=(239-(int)width)/2,offset=pass?0:1;
  for(const char *s=text;*s;s++){
   unsigned ch=(uint8_t)*s,left,w=info_glyph_width(ch,&left);if(ch>=128)ch='?';
   for(unsigned yy=0;yy<5;yy++)for(unsigned xx=0;xx<w;xx++){
    int px=x+(int)xx+offset,py=y+(int)yy+offset;
    unsigned bit=128u>>(left+xx);
    if((info_font_rows[ch][yy]&bit)&&px>=0&&px<240&&py>=0&&py<160)
     pixels[py*240+px]=(uint8_t)(pass?front_small_ink(yy?info_font_rows[ch][yy-1]:0,yy<4?info_font_rows[ch][yy+1]:0,bit):91);
   }
   x+=(int)w+1;
  }
 }
}
static UI_FN void front_join_notice(int y){
 front_info_text(y,"YOUR SAVED HERO WILL JOIN");
 front_info_text(y+8,"THE HOST'S WORLD.");
}
/* Publish to VRAM before campaign setup reuses the scene RAM and blocks. */
static UI_FN void front_connecting(unsigned *page){
 uint8_t *pixels=(uint8_t*)town_map;
 for(unsigned i=0;i<38400;i++)pixels[i]=0;
 front_text(76,"CONNECTING...",254);
 front_join_notice(94);
 front_present(page);
}
#include "frontend_ui.inc"
static UI_FN void front_movie(unsigned *page){
 frontend_mode=1;front_palette(asset_front_video_palette);front_sound_start();
 unsigned prev=(~R16(0x04000130))&1023;
 /* Timer2 /1024 supplies60Hz-independent movie timestamps. Timer1 is audio. */
 R16(0x04000108)=0;R16(0x0400010a)=0x83;unsigned last=0,time=0;
 for(unsigned f=0;f<=FRONT_OPENING_FRAMES;f++){
  /* Prepare the next image while the previous page remains visible. Waiting
   * after decoding absorbs variable delta cost instead of adding it to each
   * presentation time. The final iteration holds the last image until16s. */
  if(f<FRONT_OPENING_FRAMES){
   frontend_frame=f;
#if FRONT_OPENING_DELTA
   front_delta_decode(asset_front_opening_video,f,(uint8_t*)town_map,
       (uint8_t*)town_map+FRONT_OPENING_FRAME_BYTES,FRONT_OPENING_SCRATCH_BYTES);
#else
   lz_block(asset_front_opening_video,f,town_map,sizeof(town_map));
#endif
  }
  unsigned deadline=f*16384/FRONT_OPENING_FPS;
  while(time<deadline){
   unsigned now=R16(0x04000108);time+=(uint16_t)(now-last);last=now;
   unsigned held=(~R16(0x04000130))&1023,pressed=held&~prev;prev=held;
   if(pressed&(KEY_A|KEY_B|KEY_START)){front_sound_stop();R16(0x0400010a)=0;return;}
  }
  if(f==FRONT_OPENING_FRAMES)break;
  /* Do not silence blue audio early while predecoding the white logo. */
  if(f==FRONT_OPENING_BLUE_FRAMES)front_sound_stop();
  front_present(page);
 }
 front_sound_stop();R16(0x0400010a)=0;
}
/* The same 8-direction arrow tiles used by the gameplay automap, recolored per peer. */
static UI_FN void link_map_pixel(uint8_t *pixels,int x,int y,unsigned color){
 if(x>=60&&x<180&&y>=20&&y<140)pixels[y*240+x]=(uint8_t)color;
}
static UI_FN void link_map_line(uint8_t *pixels,int x0,int y0,int x1,int y1){
 int dx=x1-x0,dy=y1-y0,sx=dx<0?-1:1,sy=dy<0?-1:1;
 if(dx<0)dx=-dx;if(dy<0)dy=-dy;int error=dx-dy;
 for(;;){link_map_pixel(pixels,x0,y0,253);if(x0==x1&&y0==y1)break;
  int e=2*error;if(e>-dy){error-=dy;x0+=sx;}if(e<dx){error+=dx;y0+=sy;}
 }
}
static UI_FN __attribute__((target("thumb"),minsize)) void front_link_automap(uint8_t *pixels){
 int local=link_state.positions[link_state.role],lx=local&255,ly=local>>8;
 const int corners[4][2]={{0,0},{19,0},{19,10},{0,10}};
 for(int i=0;i<4;i++){
  int j=(i+1)%4,ax=corners[i][0]-lx,ay=corners[i][1]-ly,bx=corners[j][0]-lx,by=corners[j][1]-ly;
  link_map_line(pixels,120+(ax-ay)*3,80+map_round((ax+ay)*3,2),120+(bx-by)*3,80+map_round((bx+by)*3,2));
 }
 const unsigned colors[]={249,252,251,250};
 /* Local marker last so another player never hides your own position. */
 for(unsigned n=0;n<4;n++){
  unsigned i=(link_state.role+1+n)&3;if(!(link_state.members&(1u<<i)))continue;
  int dx=(link_state.positions[i]&255)-lx,dy=(link_state.positions[i]>>8)-ly;
  int x=120+(dx-dy)*3,y=80+map_round((dx+dy)*3,2);
  const uint8_t *arrow=map_arrows+(link_state.facing[i]&7)*32;
  for(int yy=0;yy<8;yy++)for(int xx=0;xx<8;xx++)
   if((arrow[yy*4+xx/2]>>((xx&1)*4))&15)link_map_pixel(pixels,x+xx-3,y+yy-3,colors[i]);
 }
 front_text(8,"PARTY AUTOMAP",254);
 front_text(144,"B EXIT",253);
}
/* Keep frontend input dispatch compact; gameplay rendering stays -O2. */
static UI_FN __attribute__((target("thumb"),minsize)) unsigned frontend_run(int opening){
 unsigned page=0,choice=0,ticks=0,prev=0,new_choice=0,credit_scroll=0,credit_repeat=0;
 display_black();loading_screen_active=0;
 R16(0x04000020)=256;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=256;
 R32(0x04000028)=0;R32(0x0400002c)=0;
 for(int i=0;i<128;i++)R16(0x07000000+i*8)=0x200;
 int saved=save_read();unsigned saved_class=saved&&(save_buffer.hero.reserved&0xff00)==0x4300?(save_buffer.hero.reserved&255):0;
 /* Save metadata shares load_buffer with logo/movie decoding. Snapshot before drawing. */
 HeroProgress saved_progress=save_buffer.hero.progress;
 unsigned saved_difficulty=saved&&save_buffer.metadata_padding2<GAME_DIFFICULTY_COUNT?save_buffer.metadata_padding2:GAME_DIFFICULTY_NORMAL;
 /* Cold boot only: the opening reuses suspended scene RAM. Invalidate
  * menu caches afterwards, then hand audio timers back to the menu mixer. */
 if(opening)front_movie(&page);
 *(uint32_t*)((uint8_t*)town_map+151680)=~0u;
 *(uint32_t*)((uint8_t*)town_map+151684)=0;
 audio_start(2);
 frontend_mode=opening?2:3;front_palette(asset_front_palette);prev=(~R16(0x04000130))&1023;
 unsigned animation_clock=audio_game_clock(),animation_time=0;
 for(;;){
  unsigned mode=frontend_mode;
  /* One animation step per six hardware refreshes, matching the main menu.
   * Cached hero screens may poll input sooner without speeding up the logo
   * or rotating selection marks. Timer0 runs at16384Hz in every frontend. */
  unsigned now=audio_game_clock();animation_time+=(uint16_t)(now-animation_clock);animation_clock=now;
  frontend_frame=animation_time/1646u;
  if(mode!=7&&mode!=8&&mode!=10&&mode!=5)lz_block(asset_front_screens,(mode==2?0:15)+frontend_frame%15,town_map,sizeof(town_map));
  if(mode==7||mode==10){
   front_hero_screen(mode,choice,saved,saved_class,&saved_progress);
  }else if(mode==8){
   uint8_t *pixels=(uint8_t*)town_map;
   for(unsigned i=0;i<38400;i++)pixels[i]=0;
   if(link_map_visible){front_link_automap(pixels);}
   else{
   front_text(8,"MULTIPLAYER LOBBY",254);
   const char *roles[]={"HOST / PLAYER 1","CLIENT / PLAYER 2","CLIENT / PLAYER 3","CLIENT / PLAYER 4"};
   front_text(24,!link_state.packets?"CONNECT 2 TO 4 SYSTEMS":roles[link_state.role],253);
   front_text(40,(link_state.launch||link_state.game_ready)?"CONNECTING...":link_state.connected?"SAVED HEROES / READY":link_state.packets?"LINK LOST / PAUSED":"WAITING FOR LINK",254);
   for(int x=19;x<=220;x++){pixels[60*240+x]=253;pixels[120*240+x]=253;}
   for(int y=60;y<=120;y++){pixels[y*240+19]=253;pixels[y*240+220]=253;}
   const unsigned colors[]={249,252,251,250};
   for(unsigned i=0;i<4;i++){
    if(!(link_state.members&(1u<<i)))continue;
    int x=20+(link_state.positions[i]&255)*10,y=61+(link_state.positions[i]>>8)*5;
    for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)pixels[(y+dy)*240+x+dx]=colors[i];
   }
   front_text(126,link_state.role?"WAIT FOR HOST / B EXIT":"A START / B EXIT",253);
   /* A timed-out host can start a local CPU party again. Clients must wait
    * for the cable host or leave the lobby. */
   front_info_text(143,(!link_state.role&&link_state.members==1)?"R: PLAY WITH CPU PARTY":"YOUR SAVED HERO WILL JOIN THE HOST'S WORLD.");
   }
  }else if(mode==9){
   front_difficulty_screen(choice);
  }else if(mode==2){front_text(139,"PRESS START",254);}
  else if(mode==3){
   front_main_screen(choice);
  }else if(mode==4){
   front_text(76,"LOAD GAME",saved?(choice==0?254:253):255);
   front_text(95,"NEW GAME",choice==1?254:253);front_text(114,"PREVIOUS MENU",choice==2?254:253);
   front_mark(76+choice*19);
  }else if(mode==5){
   front_credit_screen(credit_scroll);
  }else if(mode==6){
   front_text(76,game_difficulty_name(new_difficulty),254);front_text(94,new_choice?"CREATE NEW HERO":"START NEW GAME",254);front_text(110,new_choice?"REPLACE SAVED HERO":"KEEP HERO AND ITEMS",253);front_text(124,"RESET MAP AND SAVE",253);
   front_text(145,"B: CANCEL     A: OK",253);
  }
  front_present(&page);
  /* Cached hero screens already spend about three refreshes composing and
   * presenting. Do not add two more idle refreshes to their input loop. */
  for(int n=0;n<(mode==7||mode==10?0:2);n++)front_vblank();ticks+=3;
  unsigned held=(~R16(0x04000130))&1023,pressed=held&~prev;prev=held;
  if(mode==2){if((pressed&(KEY_A|KEY_B|KEY_START))||ticks>=420){frontend_mode=3;choice=0;ticks=0;}continue;}
  if(mode==8){
   if(!link_state.role&&link_state.members==1&&(pressed&KEY_R)){link_close();link_state.role=0;link_state.members=1;coop_cpu_mask=14;front_connecting(&page);frontend_mode=0;return 100;}
   if(!link_state.role&&link_state.connected&&!link_state.launch&&(pressed&(KEY_A|KEY_R))){link_state.cpu_mask=(pressed&KEY_R)?15u&~link_state.members:0;link_state.launch_acks=0;link_state.launch=1;}
   if((!link_state.role&&link_state.launch&&((link_state.launch_acks&link_state.members)==(link_state.members&~1u)))||link_state.game_ready){coop_cpu_mask=link_state.cpu_mask;link_state.active=0;front_connecting(&page);frontend_mode=0;return 100;}
   if(pressed&KEY_B){link_close();frontend_mode=3;choice=1;front_palette(asset_front_palette);}continue;}
  if(mode==5){
   unsigned move=pressed&(KEY_UP|KEY_DOWN);
   if(held&(KEY_UP|KEY_DOWN)){if(++credit_repeat>5&&!(credit_repeat&1))move=held&(KEY_UP|KEY_DOWN);}else credit_repeat=0;
   if((move&KEY_UP)&&credit_scroll)credit_scroll--;
   if((move&KEY_DOWN)&&credit_scroll+FRONT_CREDIT_ROWS<FRONT_CREDIT_LINES)credit_scroll++;
   if(pressed&(KEY_A|KEY_B|KEY_START)){frontend_mode=3;choice=2;front_palette(asset_front_palette);}continue;
  }
  if(mode==7){
   if(pressed&KEY_B){frontend_mode=3;choice=0;continue;}
   if(pressed&(KEY_UP|KEY_DOWN))choice=saved?!choice:1;
   if(pressed&(KEY_A|KEY_START)){if(!choice&&saved){frontend_mode=4;choice=0;}else{frontend_mode=10;choice=0;}}continue;
  }
  if(mode==10){
   if(pressed&KEY_B){frontend_mode=7;choice=1;continue;}
   if(pressed&KEY_UP)choice=(choice+2)%3;
   if(pressed&KEY_DOWN)choice=(choice+1)%3;
   if(pressed&(KEY_A|KEY_START)){new_choice=choice+1;frontend_mode=9;choice=GAME_DIFFICULTY_NORMAL;}continue;
  }
  if(mode==9){
   if(pressed&KEY_B){frontend_mode=new_choice?10:4;choice=new_choice?new_choice-1:1;continue;}
   if(pressed&KEY_UP)choice=(choice+GAME_DIFFICULTY_COUNT-1)%GAME_DIFFICULTY_COUNT;if(pressed&KEY_DOWN)choice=(choice+1)%GAME_DIFFICULTY_COUNT;
   if(pressed&(KEY_A|KEY_START)){new_difficulty=choice;if(saved){frontend_mode=6;choice=0;}else{frontend_mode=0;return new_choice+2;}}continue;
  }
  if(mode==6){
   if(pressed&KEY_B){frontend_mode=9;choice=new_difficulty;}
   else if(pressed&(KEY_A|KEY_START)){frontend_mode=0;return new_choice?new_choice+2:1;}continue;
  }
  if(pressed&KEY_B){frontend_mode=mode==3?2:mode==4?7:3;choice=0;ticks=0;continue;}
  unsigned count=mode==3?4:3;
  if(pressed&KEY_UP)choice=(choice+count-1)%count;
  if(pressed&KEY_DOWN)choice=(choice+1)%count;
  if(!(pressed&(KEY_A|KEY_START)))continue;
  if(mode==4){
   if(choice==0&&saved){frontend_mode=0;return 2;}
   if(choice==1){new_choice=0;frontend_mode=9;choice=saved_difficulty;}
   else if(choice==2){frontend_mode=7;choice=0;}
  }else if(choice==0){frontend_mode=7;choice=saved?0:1;}
  else if(choice==1){link_open();link_map_visible=0;frontend_mode=8;R16(0x05000000)=0;R16(0x050001f2)=0x021f;R16(0x050001f8)=0x7fe0;R16(0x050001f6)=0x03e0;R16(0x050001f4)=0x7c1f;}
  else if(choice==2){frontend_mode=5;credit_scroll=credit_repeat=0;R16(0x050001f8)=0x7fff;}
  else{frontend_mode=2;ticks=0;choice=0;}
 }
}
static UI_FN void frontend_enter(unsigned *animation,int opening){
 /* Restore personal settings once at cold boot, including before NEW GAME
  * keeps/replaces a hero. Returning to the title retains live option edits. */
 if(opening&&save_read()){gamma_level=save_buffer.gamma;options_volume_apply(save_buffer.metadata_padding);dpad_rotation=options_dpad_unpack(save_buffer.metadata_padding3);}
 retry_frontend:
 audio_stop();unsigned action=frontend_run(opening);audio_stop();front_sound_stop();
 if(action==100){coop_enter();*animation=0;return;}
 int valid=save_read();CharacterState hero;if(valid)hero=save_buffer.hero;
 if(action==2&&!valid){opening=0;goto retry_frontend;}
 Motion player;int restore=action==2&&valid;
 if(restore){
  if(!save_restore(&game)){opening=0;goto retry_frontend;}
  gamma_level=save_buffer.gamma;*animation=save_buffer.animation;player=save_buffer.player;
  game_ai_reset();
  game_unique_restore(&game);
 }else{
  game_set_difficulty(new_difficulty);game_seed_next((uint32_t)R16(0x0400010c)*2654435761u);
  if(action>=3){for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=0;game_create_hero(&hero,action-3);game_new_campaign(&game,&hero);}
  else if(valid){for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=saved_history(i);game_new_campaign(&game,&hero);}else{game_init(&game);game_enter_town(&game,1);}
  *animation=0;
 }
 active_game=session_hero=1;menu_mode=0;map_visible=0;map_init=1;map_last_area=-1;
 load_scene();reset_visuals();if(restore)motion[0]=player;
 draw(*animation);reveal_scene();
 /* A failed initial town commit must not become a playable dungeon session.
  * Keep the previous committed save available through LOAD GAME. */
 if(region_error){
  region_error=0;active_game=session_hero=0;
  menu_mode=4;menu_notice="NEW GAME NOT SAVED";menu_draw();
 }
 frontend_mode=0;
}
