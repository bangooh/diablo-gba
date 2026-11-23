/* Ending presentation starts only after a durable completion save.
 * Afterwards replace it atomically with a fresh personal world in town.
 * The world is never resumed after town_map becomes the Mode4 canvas. */
#include "../generated/ending_common.h"
#include "../generated/ending_video.h"
#include "../generated/ending_audio.h"
#include "ending_video.inc"
static UI_FN void ending_fade(unsigned from,unsigned to){
 for(unsigned level=from;;level+=(to>from?1:-1)){
  front_vblank();R16(0x04000050)=0x00ff;R16(0x04000054)=level;
  if(level==to)break;
 }
}
static UI_FN unsigned ending_pressed(unsigned *prev){
 unsigned held=(~R16(0x04000130))&1023,pressed=held&~*prev;*prev=held;
 return pressed&(KEY_A|KEY_B|KEY_START);
}
static UI_FN void ending_movie(unsigned *page){
 /* Every class uses the same Sorcerer movie. The old world is never resumed,
  * so scene RAM beyond the Mode4 canvas holds the previous packed frame. */
 uint8_t *pixels=(uint8_t*)town_map,*packed=pixels+38400;
 for(unsigned i=0;i<38400;i++)pixels[i]=0;
 base_palette[0]=0;dma(asset_ending_video_palette,base_palette+16,32);
 dma(base_palette,(void*)0x05000000,512);
 frontend_mode=10;unsigned next=0,prev=(~R16(0x04000130))&1023;
 audio_ending_start(0);
 for(;;){
  /* Audio owns timers1/2/3. Use its consumed-sample clock for exact5FPS. */
  unsigned target=audio_ending_position()*5/AUDIO_ENDING_SAMPLE_RATE;
  if(target>=ENDING_VIDEO_FRAMES)target=ENDING_VIDEO_FRAMES-1;
  if(next<=target){
   while(next<=target){ending_video_frame(next,(uint32_t*)packed);frontend_frame=next++;}
   for(unsigned i=0;i<ENDING_VIDEO_PACKED_BYTES;i++){
    pixels[21*240+2*i]=16+(packed[i]&15);
    pixels[21*240+2*i+1]=16+(packed[i]>>4);
   }
   front_present(page);
   /* Each frame carries its own 16-colour tint; swap it in the same VBlank. */
   dma(asset_ending_video_palette+(next-1)*32,(void*)0x05000020,32);
  }else front_vblank();
  if(ending_pressed(&prev)||!audio_ending_busy())break;
 }
 ending_fade(0,16);audio_stop();
}
/* Show a prepared canvas on the hidden page; palette 0 is the loop's, 1 the
 * final still's, 2 keeps the current one. Swap page and palette in one VBlank
 * with no fade or black frame. */
static UI_FN void ending_common_show(const uint8_t *canvas,unsigned palette,unsigned *page){
 unsigned next=*page^1;
 lighting_hardware_stop();
 dma(canvas,(void*)(0x06000000+next*0xa000),38400);
 if(palette<2)dma(asset_ending_common_palettes+palette*ENDING_COMMON_COLORS*2,
                  base_palette+ENDING_COMMON_PALETTE_BASE,ENDING_COMMON_COLORS*2);
 front_vblank();
 if(palette<2)dma(base_palette,(void*)0x05000000,512);
 R16(0x04000050)=0;R16(0x04000054)=0;
 R16(0x04000000)=0x0404|(next<<4);*page=next;
}
static UI_FN unsigned ending_common_choice(unsigned position){
 if(position>=ENDING_AUDIO_COMMON_OUTPUT_SAMPLES-10*ENDING_AUDIO_OUTPUT_RATE)return 2;
 return 1;
}
static UI_FN void ending_common(unsigned *page){
 /* The suspended scene arena holds the current loop canvas, one decoded
  * XOR delta and the final still (115200 of159744 bytes). The loop plays
  * loop0..4..1 at the audio clock's 5FPS; each step decodes one delta. */
 uint8_t *current=(uint8_t*)town_map+38400,*delta=current+38400,*final=delta+38400;
 static const uint8_t sequence[8]={0,1,2,3,4,3,2,1};
 lz_block(asset_ending_common,0,current,38400);
 lz_block(asset_ending_common,ENDING_COMMON_FINAL_CHUNK,final,38400);
 unsigned prev=(~R16(0x04000130))&1023,shown=0,step=0,closing=0;
 ending_common_show(current,0,page);
 frontend_mode=11;audio_ending_start(1);
 for(;;){
  unsigned position=audio_ending_position();
  if(ending_common_choice(position)==2){
   if(!closing){ending_common_show(final,1,page);closing=1;}else front_vblank();
  }else{
   unsigned now=position*ENDING_COMMON_LOOP_FPS/ENDING_AUDIO_OUTPUT_RATE;
   if(now!=step){
    step=now;unsigned target=sequence[step&7];
    while(shown!=target){
     unsigned next=target>shown?shown+1:shown-1;
     lz_block(asset_ending_common,next>shown?next:shown,delta,38400);
     for(unsigned i=0;i<38400/4;i++)((uint32_t*)current)[i]^=((const uint32_t*)delta)[i];
     shown=next;
    }
    ending_common_show(current,2,page);
   }else front_vblank();
  }
  if(ending_pressed(&prev)||!audio_ending_busy())break;
 }
 ending_fade(0,16);audio_stop();
}
/* Reuse the existing fresh personal-world serializer. A local hero copy is
 * required because its input may not alias the cleared save scratch. No map
 * generation is needed until the player enters a dungeon in the new game. */
static UI_FN int ending_new_campaign(void){
 if(!save_read())return 0;
 CharacterState hero=save_buffer.hero;
 unsigned difficulty=save_buffer.metadata_padding2;
 uint32_t previous=save_buffer.seed,seed=item_seed(previous^graphics_frames^0x454e4447u);
 if(!seed||seed==previous)seed=previous==1?2:1;
 if(!save_personal_begin(&hero,seed))return 0;
 save_buffer.metadata_padding2=difficulty;
 return archive_write(0,0,1);
}
static UI_FN void ending_enter(unsigned animation){
 unsigned prev=(~R16(0x04000130))&1023;
 if(coop_active)coop_wire_end();
 inventory_release();
 /* Let the original DiabloD FIFO finish; the140-tick world timer is unchanged. */
 while(audio_finale_busy()){front_vblank();prev=(~R16(0x04000130))&1023;}
 for(;;){
  if(coop_active?coop_save_session():save_write(animation))break;
  menu_mode=12;menu_draw();prev=(~R16(0x04000130))&1023;
  for(;;){
   front_vblank();unsigned held=(~R16(0x04000130))&1023,pressed=held&~prev;prev=held;
   if(pressed&KEY_A)break;
  }
 }
 shop_cancel_pending();coop_control_command=0;coop_region_clear();
 coop_active=coop_campaign_active=0;link_state.active=0;active_game=0;
 quick_mode=map_visible=menu_mode=0;
 ending_fade(0,16);display_black();loading_screen_active=0;
 R16(0x04000020)=256;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=256;
 R32(0x04000028)=0;R32(0x0400002c)=0;
 for(unsigned i=0;i<128;i++)R16(0x07000000+i*8)=0x200;
 front_palette(asset_front_palette);
 unsigned page=0;ending_movie(&page);ending_common(&page);
 /* Keep the completion save if preparing/writing the new world fails. The
  * ending canvas is still Mode4, so use its text path for the retry prompt. */
 while(!ending_new_campaign()){
  front_palette(asset_front_palette);
  for(unsigned i=0;i<38400;i++)((uint8_t*)town_map)[i]=0;
  front_text(60,"SAVE FAILED",254);front_text(80,"PREVIOUS SAVE IS SAFE",253);
  front_text(105,"A RETRY SAVE",253);front_present(&page);
  prev=(~R16(0x04000130))&1023;
  for(;;){front_vblank();unsigned held=(~R16(0x04000130))&1023,pressed=held&~prev;prev=held;if(pressed&KEY_A)break;}
  display_black();
 }
 coop_cpu_mask=0;
 display_black();frontend_mode=0;menu_mode=5;
}
