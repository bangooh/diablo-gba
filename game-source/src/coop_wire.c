#include "coop.h"
#include "link.h"
#include "audio.h"
#ifndef COOP_WIRE_REG
#define COOP_WIRE_REG(a) (*(volatile uint16_t*)(a))
#endif
#define REG(a) COOP_WIRE_REG(a)
static unsigned wire_id,wire_members,wire_seq;
typedef struct { unsigned enabled,busy,revision,dirty,map,nonce,probe,proposal,resume,missing,last_rx; } WireRoster;
static WireRoster roster __attribute__((section(".iwram.diag")))={.map=0xe4};

static uint16_t sampled;
static uint32_t elapsed,deadline;
#include "link_diag.inc"
/* Existing menu/scene rendering can legitimately hold a peer for ~2s.
 * Delay the notice until3s; the overall deadline still starts immediately. */
enum { WIRE_WAIT_TICKS=30u*16384u, WIRE_NOTICE_TICKS=3u*16384u };
static void start_deadline(uint32_t limit){roster.missing=wire_id?1:wire_members&~1u;roster.last_rx=0;sampled=audio_game_clock();elapsed=0;deadline=limit+(wire_id&&roster.enabled?5u*16384u:0);}
static int expired(void){
 uint16_t now=audio_game_clock();elapsed+=(uint16_t)(now-sampled);sampled=now;
 if(link_diag.enabled){
  link_diag.wait_ticks=elapsed;if(elapsed>link_diag.wait_max_ticks)link_diag.wait_max_ticks=elapsed;
  if(elapsed>=WIRE_NOTICE_TICKS&&elapsed<=deadline&&link_diag.stage==LD_RUNNING){
   if(!link_diag.wait_active){link_diag.wait_active=1;link_diag.wait_notices++;}
   link_diag_wait(elapsed,deadline);
  }
 }
 return elapsed>deadline;
}
static int wire_finished(int ok){
 if(ok&&link_diag.wait_active){link_diag_wait_end();link_diag.wait_active=0;link_diag.wait_recovered++;}
 return ok;
}
/* Physical D1 capture: client GOT=60D7 (BUSY+ERROR), host then times out.
 * A pending serial IF can belong to an older transfer while a new one runs.
 * GBATEK: multiplayer ERROR is undefined during BUSY. Capture completed
 * registers together; an audio/graphics IRQ must not split the snapshot.
 * The final status check also rejects a new hardware transfer starting
 * during the copy. The host's existing acknowledgement loops retry words. */
static unsigned wire_have_done;
static uint16_t wire_last_done,wire_last_sent;
enum { RECEIVE_FAILED=0,RECEIVE_OK=1,RECEIVE_RETRY=2 };
static void wire_send(uint16_t value){
 wire_last_sent=value;REG(0x0400012a)=value;if(!link_diag.error)link_diag.last_sent=value;
}
static int receive_sample(uint16_t in[4]);
#include "coop_roster.inc"
/* One completed snapshot. Also used while draining the final close ACK. */
static int receive_sample(uint16_t in[4]){
  if(!(REG(0x04000202)&0x80))return RECEIVE_FAILED;
  unsigned ime=REG(0x04000208);REG(0x04000208)=0;
  unsigned status=REG(0x04000128);
  if(status&0x80){REG(0x04000208)=(uint16_t)ime;if(!link_diag.error)link_diag.busy_polls++;return RECEIVE_FAILED;}
  for(unsigned i=0;i<4;i++)in[i]=REG(0x04000120+i*2);
  unsigned after=REG(0x04000128);
  if((after&0x80)||((status^after)&0x70)){
   REG(0x04000208)=(uint16_t)ime;if(!link_diag.error)link_diag.snapshot_retries++;return RECEIVE_FAILED;
  }
  REG(0x04000202)=0x80;REG(0x04000208)=(uint16_t)ime;
  if(status&0x40){
   /* D2 physical capture: both peers report a completed-transfer ERROR.
    * Never feed damaged words to the protocol. The host repeats the SAME
    * word, the client retains its last acknowledgement and awaits the retry.
    * Do not reset SIO mode/ID or extend the transaction's existing deadline. */
   link_diag.sio_errors++;link_diag.sio_streak++;
   if(link_diag.sio_streak>link_diag.sio_max_streak)link_diag.sio_max_streak=link_diag.sio_streak;
   link_diag.last_bad_sio=status;
   for(unsigned i=0;i<4;i++)link_diag.last_bad_rx[i]=in[i];
   /* D4 retries until the original transaction's 30-second deadline.
    * Counting errors never substitutes for receiving a valid packet. */
   return RECEIVE_RETRY;
  }
  link_diag.sio_streak=0;diag_transfer();
  if(roster_receive(in[0],status))return RECEIVE_RETRY;
  if(!roster.busy){
   if(roster.enabled&&wire_id&&((status>>4)&3)!=((roster.map>>(wire_id*2))&3))return RECEIVE_RETRY;
   roster_map(in);
   unsigned word=wire_id?in[0]:wire_last_sent,missing=0;
   for(unsigned i=1;i<4;i++)if(wire_members&(1u<<i)){
    unsigned v=in[i];int ok=(word&0xf000)==0x8000?(v&0xe000)==0x4000:(word&0xe000)==0x6000?(v&0xe000)==0x6000:v==word;
    if(!ok)missing|=1u<<i;
   }
   roster.missing=missing;roster.last_rx=elapsed;
  }
  return RECEIVE_OK;
}
unsigned coop_wire_missing(void){return wire_id&&elapsed-roster.last_rx>4096?1:roster.missing;}
static int receive_completed(uint16_t in[4],unsigned timeout_error,unsigned expected){
 for(;;){
  if(roster.enabled&&!(wire_members&(1u<<wire_id)))return link_diag_fault(LE_DEPARTED,wire_id,wire_members);
  if(expired())return link_diag_fault(timeout_error,expected,REG(0x04000202));
  int result=receive_sample(in);
  if(result==RECEIVE_RETRY&&wire_id)continue;
  if(result)return result;
 }
}
static int host_transfer_raw(uint16_t out,uint16_t in[4]){
 for(;;){
  if(wire_have_done)while((uint16_t)(audio_game_clock()-wire_last_done)<4){if(expired())return 0;}
  while((REG(0x04000128)&0x88)!=8||!(REG(0x04000134)&1)){if(expired())return 0;}
  wire_send(out);REG(0x04000202)=0x80;REG(0x04000128)=0x6083;
  int result=0;while(!result){if(expired())return 0;result=receive_sample(in);}
  wire_last_done=(uint16_t)audio_game_clock();wire_have_done=1;
  if(result==RECEIVE_OK)return 1;
 }
}
static int host_transfer(uint16_t out,uint16_t in[4]){
 for(;;){
  if(wire_members==1){in[0]=out;in[1]=in[2]=in[3]=0xffff;return 1;}
  if(host_transfer_raw(out,in))return 1;
  if(roster.enabled&&roster_recover(wire_members))continue;
  return link_diag_fault(LE_TRANSFER_TIMEOUT,0x80,REG(0x04000128));
 }
}
static int host_echo(uint16_t word){
 uint16_t in[4];for(;;){if(!host_transfer(word,in))return 0;unsigned ok=1;
 for(unsigned i=1;i<4;i++)if((wire_members&(1u<<i))&&in[i]!=word)ok=0;
 if(ok)return 1;
 }
}
static int wire_begin(unsigned id,unsigned members){
 if(id>=4||(members&~15u)||!(members&1)||!(members&(1u<<id)))return link_diag_fault(LE_TOPOLOGY,id,members);
 diag_phase(LP_BEGIN);
 wire_id=id;wire_members=members;wire_seq=0;roster=(WireRoster){.map=ROSTER_IDENTITY};wire_have_done=0;if(members==1)return id==0;start_deadline(WIRE_WAIT_TICKS);
 REG(0x04000128)=0x6003;REG(0x04000202)=0x80;wire_send(LINK_SYNC);
 if(!id)return host_echo(LINK_SYNC);
 for(;;){uint16_t in[4];if(!receive_completed(in,LE_RECEIVE_TIMEOUT,LINK_SYNC))return 0;
 if(in[0]==LINK_SYNC)return 1;
 }
}
int coop_wire_begin(unsigned id,unsigned members){return wire_finished(wire_begin(id,members));}
static int wire_frame(uint16_t local,uint16_t values[4],uint16_t hash,uint32_t limit,unsigned pending,unsigned *control){
 if(wire_members==1){values[0]=local&4095;values[1]=values[2]=values[3]=0;if(control)*control=pending;wire_seq++;return 1;}
 start_deadline(limit);unsigned seq=++wire_seq&0x7ff;
 diag_phase(LP_REQUEST);
 if(!wire_id){
  uint16_t in[4];values[0]=local&4095;unsigned any=pending;
  for(;;){if(!host_transfer((uint16_t)(0x8000|seq),in))return 0;unsigned ok=1;
   for(unsigned i=1;i<4;i++)if(wire_members&(1u<<i)){
    if((in[i]&0xe000)!=0x4000)ok=0;else{values[i]=in[i]&4095;any|=(in[i]>>12)&1;}
   }else values[i]=0;
   if(ok)break;
  }
  for(unsigned i=0;i<4;i++){diag_phase(LP_VALUE0+i);if(!host_echo((uint16_t)((0x9000+i*0x1000)|values[i])))return 0;}
  diag_phase(LP_HASH);if(!host_echo((uint16_t)(0xd000|(hash&4095))))return 0;
  diag_phase(LP_COMMIT);if(control)*control=any;return host_echo((uint16_t)(0xe000|seq|(any?0x800:0)));
 }
 unsigned seen=0;uint16_t response=wire_last_sent;
 for(;;){
  uint16_t in[4];if(!receive_completed(in,LE_RECEIVE_TIMEOUT,seq))return 0;
  uint16_t word=in[0];unsigned type=word>>12;
  if(word==(0x8000|seq)){response=(uint16_t)(0x4000|(pending?0x1000:0)|(local&4095));seen=1;}
  else if(type>=9&&type<=12&&seen){unsigned i=type-9;
   if((seen&((1u<<(i+1))-1))==((1u<<(i+1))-1)){
    diag_phase(LP_VALUE0+i);if(i==wire_id&&(word&4095)!=(local&4095))return link_diag_fault(LE_LOCAL_VALUE,local&4095,word&4095);
    values[i]=word&4095;seen|=1u<<(i+1);response=word;
   }
  }else if(type==13&&seen==31){diag_phase(LP_HASH);if((word&4095)!=(hash&4095))return link_diag_fault(LE_HASH,hash&4095,word&4095);response=word;seen=63;}
  else if((word&~0x800u)==(0xe000|seq)&&seen==63){diag_phase(LP_COMMIT);wire_send(word);if(control)*control=(word>>11)&1;return 1;}
  wire_send(response);
 }
}
int coop_wire_frame(uint16_t local,uint16_t values[4],uint16_t hash){return wire_finished(wire_frame(local,values,hash,WIRE_WAIT_TICKS,0,0));}
int coop_wire_commit_frame(uint16_t local,uint16_t values[4],uint16_t hash){return wire_finished(wire_frame(local,values,hash,WIRE_WAIT_TICKS,0,0));}
#include "coop_block.inc"
void coop_wire_end(void){roster.enabled=0;REG(0x04000128)=0;REG(0x04000134)=0x8000;REG(0x04000202)=0x80;}

/* A normal frame's final ACK is still pending when a client returns. Do not
 * disable its SIO while the host is collecting that ACK. All peers call this
 * after receiving all save results; F105 is not a gameplay/control word.
 * Clients keep acknowledging CLOSE until the host stops clocking for 1s.
 * Even corrupt transfers count as activity, but never as a valid CLOSE.
 * The overall close operation remains bounded by the original 30s deadline. */
int coop_wire_finish(void){
 enum { CLOSE_WORD=0xf105, CLOSE_QUIET=16384 };
 if(wire_members==1)return 1;
 start_deadline(WIRE_WAIT_TICKS);diag_phase(LP_CLOSE);
 if(!wire_id)return wire_finished(host_echo(CLOSE_WORD));
 unsigned closing=0;uint32_t last_activity=0;
 for(;;){
  if(expired())return closing?1:link_diag_fault(LE_RECEIVE_TIMEOUT,CLOSE_WORD,wire_last_sent);
  uint16_t in[4];int result=receive_sample(in);
  if(result){
   last_activity=elapsed;
   if(result==RECEIVE_OK&&in[0]==CLOSE_WORD){closing=1;wire_send(CLOSE_WORD);}
  }
  if(closing&&elapsed-last_activity>=CLOSE_QUIET)return 1;
 }
}

/* Piggyback the control-channel presence flag on the input acknowledgement. */
static int wire_input(uint16_t local,uint16_t values[4],uint16_t hash,unsigned pending,unsigned *control){
 if(wire_members==1){if(roster.dirty){roster.dirty=0;return 16|wire_members;}return wire_frame(local,values,hash,0,pending,control);}
 start_deadline(WIRE_WAIT_TICKS);unsigned seq=++wire_seq&0x7ff,any=pending;
 diag_phase(LP_REQUEST);
 if(!wire_id){
  uint16_t in[4];
  if(roster.dirty){
   unsigned members=wire_members,word=0x0800|((seq&127)<<4)|members;
   if(!host_echo((uint16_t)word))return 0;
   roster.dirty=wire_members!=members;wire_seq--;return 16|members;
  }
  unsigned revision;
  restart_input:revision=roster.revision;any=pending;values[0]=local&4095;
  for(;;){if(!host_transfer((uint16_t)(0x8000|seq),in))return 0;unsigned ready=1;
   for(unsigned i=1;i<4;i++)if(wire_members&(1u<<i)){
    if((in[i]&0xe000)!=0x4000)ready=0;else{values[i]=in[i]&4095;any|=(in[i]>>12)&1;}
   }else values[i]=0;
   if(ready)break;
  }
  if(revision!=roster.revision)goto restart_input;
  /* Every SIO transfer delivers all four registers to every peer. Retain
   * input bits in the acknowledgement so a late client can still latch them. */
  diag_phase(LP_INPUT_VALUES);uint16_t own=(uint16_t)(0x6000|(pending?0x1000:0)|(local&4095));
  for(;;){if(!host_transfer(own,in))return 0;unsigned ready=1;
   for(unsigned i=1;i<4;i++)if((wire_members&(1u<<i))&&((in[i]&0xe000)!=0x6000||(in[i]&4095)!=values[i]))ready=0;
   if(ready)break;
  }
  if(revision!=roster.revision)goto restart_input;
  diag_phase(LP_HASH);if(!host_echo((uint16_t)(0xd000|(hash&4095))))return 0;
  if(revision!=roster.revision)goto restart_input;
  diag_phase(LP_COMMIT);*control=any;return host_echo((uint16_t)(0xe000|seq|(any?0x800:0)));
 }
 unsigned seen=0;uint16_t response=wire_last_sent;
 for(;;){
  uint16_t in[4];if(!receive_completed(in,LE_RECEIVE_TIMEOUT,seq))return 0;
  uint16_t word=in[0];
  if((word&0xfff0)==(0x0800|((seq&127)<<4))&&(word&1)){
   unsigned members=word&15;wire_send(word);roster.dirty=wire_members!=members;wire_seq--;return 16|members;
  }
  if(word==(0x8000|seq)){response=(uint16_t)(0x4000|(pending?0x1000:0)|(local&4095));seen=1;}
  else if((word&0xe000)==0x6000&&seen==1){
   diag_phase(LP_INPUT_VALUES);
   unsigned ready=1;any=0;
   for(unsigned i=0;i<4;i++)if(wire_members&(1u<<i)){
    unsigned v=in[i];
    if((v&0xc000)!=0x4000)ready=0;
    values[i]=v&4095;any|=(v>>12)&1;
   }else values[i]=0;
   if(ready){if(values[wire_id]!=(local&4095))return link_diag_fault(LE_LOCAL_VALUE,local&4095,values[wire_id]);seen=2;response=(uint16_t)(0x6000|(pending?0x1000:0)|(local&4095));}
  }else if((word>>12)==13&&seen>=2){diag_phase(LP_HASH);if((word&4095)!=(hash&4095))return link_diag_fault(LE_HASH,hash&4095,word&4095);seen=3;response=word;}
  else if((word&~0x800u)==(0xe000|seq)&&seen==3){diag_phase(LP_COMMIT);if(((word>>11)&1)!=any)return link_diag_fault(LE_CONTROL,any,(word>>11)&1);wire_send(word);*control=any;return 1;}
  wire_send(response);
 }
}
int coop_wire_input(uint16_t local,uint16_t values[4],uint16_t hash,unsigned pending,unsigned *control){
 return wire_finished(wire_input(local,values,hash,pending,control));
}
