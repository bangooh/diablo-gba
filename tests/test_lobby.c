#include <assert.h>
#include <stdio.h>
#include "link.h"
static void packet(LinkState *from,LinkState *to,unsigned peer){
 for(unsigned i=0;i<LINK_WORDS;i++)link_receive(to,peer,link_next_word(from));
}
static void ack_start(LinkState *h,LinkState c[4],unsigned mask){
 h->launch=1;h->launch_acks=0;h->cpu_mask=0;h->tx_index=0;
 for(unsigned i=1;i<4;i++)if(mask&(1u<<i)){
  packet(h,&c[i],0);assert(c[i].launch);c[i].tx_index=0;packet(&c[i],h,i);
 }
 assert((h->launch_acks&mask)==(mask&~1u));
}
int main(void){unsigned cases=0;
for(unsigned mask=3;mask<16;mask+=2)for(unsigned leaving=1;leaving<4;leaving++)if(mask&(1u<<leaving))for(unsigned launching=0;launching<2;launching++){
 LinkState h,c[4];link_reset(&h,0);
 for(unsigned i=1;i<4;i++){link_reset(&c[i],i);if(mask&(1u<<i))packet(&c[i],&h,i);}
 assert(h.members==mask&&h.connected);h.launch=launching;h.launch_acks=mask&~1u;h.cpu_mask=launching?(15&~mask):0;
 for(unsigned tick=0;tick<LINK_TIMEOUT;tick++){
  for(unsigned i=1;i<4;i++)if(i!=leaving&&(mask&(1u<<i)))packet(&c[i],&h,i);
  link_frame(&h,0);
  if(tick<LINK_TIMEOUT-1)assert(h.members==mask);
 }
 unsigned remaining=mask&~(1u<<leaving);
 assert(h.members==remaining&&h.connected==(remaining!=1));assert(!h.launch&&!h.launch_acks&&!h.game_ready&&!h.cpu_mask);
 assert(!(h.have_sequence&(1u<<leaving))&&!h.remote_keys[leaving]);
 for(unsigned i=1;i<4;i++)if(remaining&(1u<<i)){h.tx_index=0;packet(&h,&c[i],0);assert(c[i].members==remaining&&!c[i].launch);}
 if(remaining!=1)ack_start(&h,c,remaining);
 // A returning client begins a fresh packet sequence; stale acknowledgments cannot carry over.
 link_reset(&c[leaving],leaving);packet(&c[leaving],&h,leaving);assert(h.members==mask&&!h.launch&&!h.launch_acks);
 ack_start(&h,c,mask);cases++;
}
/* Host timeout cancels launches, leaves only self visible, and accepts a
 * restarted host sequence without interrupting future client packets. */
for(unsigned role=1;role<4;role++)for(unsigned launching=0;launching<2;launching++){
 LinkState h,c;link_reset(&h,0);link_reset(&c,role);
 h.members=15;h.connected=1;h.sequence=300;h.launch=launching;
 packet(&h,&c,0);c.game_ready=launching;c.launch_acks=14;
 for(unsigned i=0;i<LINK_TIMEOUT;i++)link_frame(&c,0);
 assert(c.members==(1u<<role)&&!c.connected&&!c.launch&&!c.launch_acks&&!c.game_ready&&!c.cpu_mask&&!c.have_sequence);
 link_next_word(&c);unsigned next=c.tx_index;link_frame(&c,0);assert(c.tx_index==next);
 link_reset(&h,0);h.members=1|(1u<<role);h.connected=1;
 packet(&h,&c,0);assert(c.connected&&c.members==h.members&&c.received[0]==1);cases++;
}
/* Offline availability is independent of hardware role. */
for(unsigned role=0;role<4;role++){
 LinkState s;link_reset(&s,role);assert(link_can_start_cpu(&s));
 s.connected=1;assert(!link_can_start_cpu(&s));s.connected=0;
 s.launch=1;assert(!link_can_start_cpu(&s));s.launch=0;
 s.game_ready=1;assert(!link_can_start_cpu(&s));s.game_ready=0;
 s.packets=1;s.members=15;assert(!link_can_start_cpu(&s));
 s.members=1u<<role;assert(link_can_start_cpu(&s));cases++;
}
printf("PASS: %u roster departure/rejoin/launch scenarios across 2-4 player masks\n",cases);}
