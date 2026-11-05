/* Direct Mode3 VRAM only: never borrow town_map/load_buffer during campaign
 * generation. Live output changes on stage boundaries, not on each packet.
 * No VBlank wait or link polling is introduced into the wire transaction. */
#include "link_diag.h"
extern volatile unsigned audio_effect_count;
#include "link_recovery_ui.inc"
static const char *const diag_stages[]={"NONE","READ LOCAL SAVE","CHECK EMPTY SAVE","WIRE SYNC","CHECK LOCAL HERO","SEND HEROES","CPU SEED","CHECK ALL HEROES","READ HOST WORLD","HOST READY","SEND HEADER","SEND METADATA","SEND FLOOR","CHECK WORLD DATA","BUILD WORLD","WORLD READY","COMPARE WORLD","COMMIT READY","HOST SAVE WRITE","SAVE COMMIT ACK","COMPARE EPOCH","APPLY WORLD","LOAD SCENE","IN GAME","END SESSION READY","SAVE LOCAL GAME","SHARE SAVE RESULTS","CLOSE LINK","SESSION ENDED"};
static const char *const diag_errors[]={"NO ERROR","SAVE INVALID OR UNREADABLE","INVALID LINK MEMBERS","PEERS NOT READY TIMEOUT","SERIAL COMPLETION TIMEOUT","RECEIVE OR ECHO TIMEOUT","SERIAL HARDWARE ERROR","OWN VALUE MISMATCH","WIRE HASH MISMATCH","CONTROL FLAG MISMATCH","PEER REJECTED DATA","INVALID TRANSFER BYTE","WORLD OR EPOCH MISMATCH","INVALID HERO","HOST SAVE READ FAILED","HOST FLOOR READ FAILED","WORLD DATA INVALID","WORLD BUILD FAILED","HOST SAVE WRITE FAILED","COMMIT REJECTED","OTHER SESSION FAILURE","BLOCK HEADER INVALID","BLOCK CRC RETRIES FAILED","LEFT ACTIVE PARTY"};
static UI_FN void diag_text(unsigned x,unsigned y,const char *s,unsigned scale,unsigned color){
 const char *letters=OVERLAY_GLYPHS;volatile uint16_t *v=(volatile uint16_t*)0x06000000;
 for(;*s;s++,x+=4*scale){
  unsigned ch=(uint8_t)*s,n=0;while(letters[n]&&letters[n]!=ch)n++;
  for(unsigned yy=0;yy<5;yy++){
   unsigned bits=ch=='.'?(yy==4?2:0):ch=='-'?(yy==2?7:0):ch==':'?((yy==1||yy==3)?2:0):ch=='/'?(4u>>(yy*3/5)):letters[n]?overlay_rows[n][yy]:0;
   for(unsigned xx=0;xx<3;xx++)if(bits&(4u>>xx))for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++){
    unsigned px=x+xx*scale+dx,py=y+yy*scale+dy;if(px<240&&py<160)v[py*240+px]=(uint16_t)color;
   }
  }
 }
}
static UI_FN void diag_hex(unsigned x,unsigned y,unsigned value,unsigned digits){
 char text[9];const char *hex="0123456789ABCDEF";
 for(unsigned i=0;i<digits;i++)text[i]=hex[(value>>((digits-i-1)*4))&15];text[digits]=0;
 diag_text(x,y,text,1,0x7fff);
}
static UI_FN void diag_code(unsigned y,int failed){
 char text[]="S00 E00";text[1]='0'+link_diag.stage/10; text[2]='0'+link_diag.stage%10;
 text[5]='0'+link_diag.error/10;text[6]='0'+link_diag.error%10;
 diag_text(8,y,text,2,failed?0x211f:0x3bff);
}
static UI_FN void diag_prepare(void){
 info_stream_stop();lighting_hardware_stop();R16(0x04000000)=0x0080;
 for(unsigned i=0;i<38400;i++)R16(0x06000000+i*2)=0;
 R16(0x04000050)=0;R16(0x04000054)=0;
 R16(0x04000020)=256;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=256;
 R32(0x04000028)=R32(0x0400002c)=0;R16(0x04000000)=0x0403;
 diag_text(8,6,"DIABLO GBA",2,0x3bff);
}
static UI_FN void diag_peer(unsigned y){
 diag_text(8,y,link_diag.id?"CLIENT P":"HOST   P",1,0x7fff);diag_hex(44,y,link_diag.id+1,1);
 diag_text(60,y,"MASK",1,0x7fff);diag_hex(84,y,link_diag.members,2);
 diag_text(112,y,"PROTOCOL D108",1,0x7fff);
}
static UI_FN void front_exit_notice(void);
static UI_FN void link_diag_end_screen(unsigned saved,unsigned all_saved){
 diag_prepare();
 for(unsigned y=6;y<16;y++)for(unsigned x=8;x<232;x++)R16(0x06000000+2*(y*240+x))=0;
 if(saved==2){front_exit_notice();return;}
 diag_text(8,6,saved==2?"ENDING SESSION":saved?"SESSION ENDED":"SAVE FAILED",2,saved?0x3bff:0x211f);
 diag_peer(40);
 diag_text(8,65,saved==2?"SAVING ALL PLAYERS":saved?"YOUR GAME SAVED":"HERO AND WORLD RETAINED",1,0x7fff);
 if(saved!=2&&!all_saved&&saved)diag_text(8,85,"ANOTHER PLAYER SAVE FAILED",1,0x211f);
 diag_text(8,110,saved==2?"PLEASE WAIT":saved?"A - RETURN TO TITLE":"A - RETRY SAVE",1,0x7fff);
}
UI_FN void link_diag_draw_stage(void){
 if(link_diag.stage==LD_SAVE){
  diag_prepare();diag_text(8,27,"CONNECTING",1,0x7fff);
  diag_peer(82);diag_text(8,120,"PLEASE WAIT",1,0x7fff);
 }
 for(unsigned y=46;y<74;y++)for(unsigned x=8;x<232;x++)R16(0x06000000+2*(y*240+x))=0;
 diag_code(46,0);
 diag_text(8,64,link_diag.stage<=LD_LEAVE_DONE?diag_stages[link_diag.stage]:"UNKNOWN",2,0x7fff);
}
UI_FN void link_diag_halt(void){
 /* The wire already recorded the cause; do not replace it with the peer's
  * later timeout. Saving is not attempted by this reporting screen. */
 if(!link_diag.error)link_diag_fault(LE_OTHER,0,0);
 link_diag.stop_menu=menu_mode;link_diag.stop_quick=quick_mode;link_diag.stop_effects=audio_effect_count;
 link_diag_wait_end();coop_wire_end();link_state.active=0;audio_stop();diag_prepare();
 if(link_diag.error>=LE_READY_TIMEOUT&&link_diag.error<=LE_SIO){
  for(unsigned y=6;y<16;y++)for(unsigned x=8;x<232;x++)R16(0x06000000+2*(y*240+x))=0;
  diag_text(8,6,"CONNECTION LOST",2,0x211f);
 }
 diag_text(8,23,"MULTIPLAYER ERROR",1,0x211f);diag_code(35,1);
 diag_text(8,50,link_diag.stage<=LD_LEAVE_DONE?diag_stages[link_diag.stage]:"UNKNOWN",1,0x7fff);
 diag_text(8,61,link_diag.error<=LE_DEPARTED?diag_errors[link_diag.error]:"UNKNOWN ERROR",1,0x211f);
 diag_peer(73);
 diag_text(8,84,"POS",1,0x7fff);diag_hex(24,84,link_diag.progress,4);diag_text(40,84,"/",1,0x7fff);diag_hex(44,84,link_diag.total,4);
 diag_text(72,84,"SEQ",1,0x7fff);diag_hex(88,84,link_diag.sequence,4);diag_text(120,84,"PH",1,0x7fff);diag_hex(132,84,link_diag.phase,2);
 diag_text(8,94,"SIO",1,0x7fff);diag_hex(24,94,link_diag.sio,4);diag_text(48,94,"IF",1,0x7fff);diag_hex(60,94,link_diag.irq,4);diag_text(88,94,"RCNT",1,0x7fff);diag_hex(108,94,link_diag.rcnt,4);
 diag_text(8,104,"TX",1,0x7fff);diag_hex(20,104,link_diag.tx,4);diag_text(44,104,"RX",1,0x7fff);for(unsigned i=0;i<4;i++)diag_hex(56+i*24,104,link_diag.rx[i],4);
 diag_text(160,104,"SW",1,0x7fff);diag_hex(172,104,link_diag.last_sent,4);
 diag_text(8,114,"TIME",1,0x7fff);diag_hex(28,114,link_diag.elapsed,8);diag_text(60,114,"/",1,0x7fff);diag_hex(64,114,link_diag.limit,8);
 diag_text(112,114,"N",1,0x7fff);diag_hex(120,114,link_diag.transfers,8);
 diag_text(164,114,"BW",1,0x7fff);diag_hex(176,114,link_diag.busy_polls,8);
 diag_text(8,124,"EXP",1,0x7fff);diag_hex(24,124,link_diag.expected,8);diag_text(64,124,"GOT",1,0x7fff);diag_hex(80,124,link_diag.actual,8);
 diag_text(128,124,"RR",1,0x7fff);diag_hex(140,124,link_diag.snapshot_retries,8);
 diag_text(8,134,"FLASH",1,0x7fff);diag_hex(32,134,link_diag.flash_status,8);diag_text(72,134,"ID",1,0x7fff);diag_hex(84,134,link_diag.flash_device,4);diag_text(108,134,"READ",1,0x7fff);diag_hex(128,134,link_diag.flash_read,2);
 diag_text(144,134,"LERR",1,0x7fff);diag_hex(168,134,link_diag.lobby_errors,4);
 diag_text(8,143,"ER",1,0x7fff);diag_hex(20,143,link_diag.sio_errors,8);
 diag_text(56,143,"ST",1,0x7fff);diag_hex(68,143,link_diag.sio_max_streak,4);
 diag_text(88,143,"LS",1,0x7fff);diag_hex(100,143,link_diag.last_bad_sio,4);
 diag_text(120,143,"RC",1,0x7fff);diag_hex(132,143,link_diag.wait_recovered,4);
 diag_text(152,143,"WT",1,0x7fff);diag_hex(164,143,link_diag.wait_max_ticks,8);
 diag_text(200,143,"ME",1,0x7fff);diag_hex(212,143,link_diag.stop_menu,2);
 diag_text(8,153,"RESTART TO RETURN TO TITLE",1,0x3bff);
 link_diag.stopped=1;
 for(;;)__asm__ volatile("" ::: "memory");
}
