#include "link.h"
#define UP 64u
#define DOWN 128u
#define LEFT 32u
#define RIGHT 16u
#define START 8u
uint16_t link_crc(const uint16_t *p){
 uint16_t crc=0xffff;
 for(unsigned i=0;i<LINK_WORDS-1;i++)for(unsigned byte=0;byte<2;byte++){
  crc^=(uint16_t)((p[i]>>(byte*8))&255)<<8;
  for(unsigned bit=0;bit<8;bit++)crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
 }
 return crc;
}
static void spawn(LinkState *s){for(unsigned i=0;i<4;i++){s->positions[i]=(4+i*4)|(6<<8);s->facing[i]=0;}}
void link_reset(LinkState *s,unsigned role){
 *s=(LinkState){0};s->role=role;s->members=1;s->active=1;spawn(s);
}
static void move(uint16_t *p,uint16_t *facing,unsigned keys){
 int x=*p&255,y=*p>>8;
 int dx=!!(keys&RIGHT)-!!(keys&LEFT),dy=!!(keys&DOWN)-!!(keys&UP);
 /* World axes project diagonally on the isometric automap: S SW W NW N NE E SE. */
 if(dx||dy){static const uint8_t directions[9]={4,5,6,3,0,7,2,1,0};*facing=directions[(dy+1)*3+dx+1];}
 x+=dx;y+=dy;
 if(x<1)x=1;if(x>18)x=18;if(y<1)y=1;if(y>9)y=9;
 *p=(uint16_t)(x|(y<<8));
}
static int valid_position(uint16_t p){return (p&255)>=1&&(p&255)<=18&&(p>>8)>=1&&(p>>8)<=9;}
static void cancel_launch(LinkState *s){
 s->launch=s->launch_acks=s->game_ready=s->cpu_mask=0;
 /* Discard any queued launch packet when the lobby roster changes. */
 s->tx_index=0;
}
static void host_status(LinkState *s){
 unsigned removed=0;
 for(unsigned i=1;i<4;i++)if((s->members&(1u<<i))&&s->age[i]>=LINK_TIMEOUT){
  removed|=1u<<i;s->remote_keys[i]=0;s->received[i]=0;
  for(unsigned j=0;j<LINK_WORDS;j++)s->window[i][j]=0;
 }
 if(removed){s->members&=~removed;s->have_sequence&=~removed;cancel_launch(s);}
 s->connected=s->members!=1;
}
void link_receive(LinkState *s,unsigned peer,uint16_t word){
 if(peer>3||peer==s->role||(s->role&&peer))return;
 uint16_t *p=s->window[peer];
 for(unsigned i=0;i<LINK_WORDS-1;i++)p[i]=p[i+1];p[LINK_WORDS-1]=word;
 if(p[0]!=LINK_MAGIC)return;
 if(p[LINK_WORDS-1]!=link_crc(p)){s->errors++;return;}
 if(p[1]!=(s->role?LINK_STATE:LINK_INPUT)){s->errors++;return;}
 unsigned delta=(uint16_t)(p[2]-s->received[peer]);
 if((s->have_sequence&(1u<<peer))&&(!delta||delta>=0x8000))return;
 if(s->role){
  for(unsigned i=0;i<4;i++)if(!valid_position(p[3+i])){s->errors++;return;}
  if((p[7]&~0x3fffu)||(p[8]&~0x1ffu)||!(p[8]&1)){s->errors++;return;}
  for(unsigned i=0;i<4;i++)s->positions[i]=p[3+i];
  s->launch=(p[7]>>13)&1;s->scene=p[7]&1;for(unsigned i=0;i<4;i++)s->facing[i]=(p[7]>>(1+i*3))&7;
  s->members=p[8]&15;s->cpu_mask=(p[8]>>4)&15;
  if(s->cpu_mask&&(s->cpu_mask!=(15u&~s->members))){s->errors++;s->launch=0;return;}
  s->connected=!!(p[8]&0x100)&&!!(s->members&(1u<<s->role));
 }else{
  if(p[3]&~1023u||p[4]!=LINK_VERSION||p[5]!=peer||p[6]>1||p[7]>15||p[8]){s->errors++;return;}
  if(!(s->members&(1u<<peer)))cancel_launch(s);
  if(p[6]&&s->launch&&p[7]==s->cpu_mask)s->launch_acks|=1u<<peer;
  s->remote_keys[peer]=p[3];s->members|=1u<<peer;
 }
 s->received[peer]=p[2];s->have_sequence|=1u<<peer;s->age[peer]=0;s->packets++;
 if(!s->role)host_status(s);
}
void link_frame(LinkState *s,unsigned keys){
 s->keys=keys&1023;
 for(unsigned i=0;i<4;i++){
  if(s->age[i]<LINK_TIMEOUT)s->age[i]++;
  if(s->age[i]>=LINK_TIMEOUT)s->remote_keys[i]=0;
 }
 if(s->role){
  /* Without the host, no remote roster is authoritative. Keep only this
   * device visible and discard the old launch/sequence before reconnecting. */
  if(s->age[0]>=LINK_TIMEOUT&&(s->members!=(1u<<s->role)||s->connected||s->launch||s->game_ready)){
   s->connected=0;s->members=1u<<s->role;cancel_launch(s);
   s->have_sequence=0;s->received[0]=0;
   for(unsigned j=0;j<LINK_WORDS;j++)s->window[0][j]=0;
  }
 }
 else{
  host_status(s);
  if(s->connected&&(keys&START)&&!s->last_start){s->scene^=1;spawn(s);}
 }
 s->last_start=!!(keys&START);
}
uint16_t link_next_word(LinkState *s){
 if(!s->tx_index){
  uint16_t *p=s->tx;p[0]=LINK_MAGIC;p[1]=s->role?LINK_INPUT:LINK_STATE;p[2]=++s->sequence;
  if(s->role){p[3]=s->keys;p[4]=LINK_VERSION;p[5]=s->role;p[6]=s->launch;p[7]=s->cpu_mask;p[8]=0;}
  else{
   if(s->connected)for(unsigned i=0;i<4;i++)if(s->members&(1u<<i))move(&s->positions[i],&s->facing[i],i?s->remote_keys[i]:s->keys);
   for(unsigned i=0;i<4;i++)p[3+i]=s->positions[i];
   p[7]=s->scene|(s->launch<<13);for(unsigned i=0;i<4;i++)p[7]|=s->facing[i]<<(1+i*3);
   p[8]=s->members|(s->cpu_mask<<4)|(s->connected?0x100:0);
  }
  p[LINK_WORDS-1]=link_crc(p);
 }
 uint16_t w=s->tx[s->tx_index];s->tx_index=(s->tx_index+1)%LINK_WORDS;return w;
}
#ifndef LINK_HOST_TEST
#define REG(a) (*(volatile uint16_t*)(a))
LinkState link_state;
static unsigned pending,delay_frames,assigned,topology_error,transfer_gap;
void link_open(void){
 REG(0x04000134)=0;REG(0x04000128)=0x6003; /* multi, IRQ flag,115200; serial IE remains masked */
 REG(0x04000202)=0x80;link_reset(&link_state,!!(REG(0x04000128)&4));pending=0;delay_frames=0;assigned=topology_error=transfer_gap=0;
 REG(0x0400012a)=link_next_word(&link_state);
}
void link_close(void){
 /* A client leaving the UI must not break the cable READY chain for the
  * remaining players. Keep the serial peripheral passive in multiplayer
  * mode, transmitting only an invalid word until the next link_open(). */
 if(link_state.role){REG(0x0400012a)=0xffff;REG(0x04000134)=0;REG(0x04000128)=0x6003;}
 else{REG(0x04000128)=0;REG(0x04000134)=0x8000;}
 REG(0x04000202)=0x80;link_state.active=0;pending=0;
}
void link_poll(void){
 if(!link_state.active)return;
 if(REG(0x04000202)&0x80){
  if(link_state.role&&link_state.launch&&REG(0x04000120)==LINK_SYNC){link_state.game_ready=1;REG(0x04000202)=0x80;return;}
  REG(0x04000202)=0x80;unsigned status=REG(0x04000128),id=(status>>4)&3;pending=0;
  if(!assigned){
   link_state.role=id;assigned=1;
   /* Only the magic word has been sent: finalize the cable-assigned sender ID. */
   if(id){link_state.tx[5]=(uint16_t)id;link_state.tx[LINK_WORDS-1]=link_crc(link_state.tx);}
  }
  else if(id!=link_state.role)topology_error=1;
  /* A departing slot may set SIO ERROR while surviving slots still deliver
   * valid words. Per-packet magic/version/CRC/sequence validation remains the
   * authority; dropping the entire transfer also times out healthy peers. */
  if(status&0x40)link_state.errors++;
  if(!topology_error){
   for(unsigned peer=0;peer<4;peer++)if(peer!=id)link_receive(&link_state,peer,REG(0x04000120+peer*2));
  }else{link_state.errors++;link_state.connected=0;}
  REG(0x0400012a)=link_next_word(&link_state);
 }
}
void link_refresh(unsigned keys){
 if(!link_state.active)return;
 link_poll();link_frame(&link_state,keys);
 if(topology_error){link_state.connected=0;return;}
 unsigned status=REG(0x04000128);
 if(pending){
  if(++delay_frames>LINK_TIMEOUT){REG(0x04000128)=0x6003;pending=0;delay_frames=0;link_state.connected=0;}
  return;
 }
 if(!(status&4)&&(status&8)&&!(status&0x80)){
  /* Menu drawing is not phase-aligned across devices. Keep one full UI
   * frame between words so even a peer drawing its page polls completion. */
  if(++transfer_gap<3)return;transfer_gap=0;
  REG(0x04000128)=0x6083;pending=1;delay_frames=0;
 }
}
#endif
