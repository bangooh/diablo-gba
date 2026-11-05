/* D108: transport membership can shrink without changing the game mid-tick.
 * Logical hero slots never move. A fresh probe binds them to current SIO IDs.
 * All normal data still requires an error-free completed hardware snapshot.
 * The next INPUT boundary publishes a fixed game mask and checks full state.
 */
enum { ROSTER_PROBE=0x1000,ROSTER_PROPOSE=0x2000,ROSTER_COMMIT=0x3000,
 ROSTER_WINDOW=16384u,ROSTER_IDENTITY=0xe4 };
static int roster_valid(unsigned word){
 unsigned mask=(word>>8)&15,map=word&255,slots=0;
 if(!(mask&1)||(mask&~wire_members)||(map&3))return 0;
 for(unsigned i=0;i<4;i++)if(mask&(1u<<i)){
  unsigned slot=(map>>(2*i))&3;if(slots&(1u<<slot))return 0;slots|=1u<<slot;
 }
 return 1;
}
static int roster_receive(unsigned word,unsigned status){
 if(!wire_id||!roster.enabled)return 0;
 unsigned type=word&0xf000;
 if(type==ROSTER_PROBE){
  if(roster.probe!=word){
   if(!roster.probe)roster.resume=wire_last_sent;
   roster.probe=word;roster.proposal=0;
  }
  /* A live host is re-forming the session; allow its bounded handshake. */
  sampled=audio_game_clock();elapsed=0;deadline=5u*16384u;
  wire_send((uint16_t)((word&~3u)|wire_id));return 1;
 }
 if(type==ROSTER_PROPOSE&&roster.probe&&roster_valid(word)){
  unsigned mask=(word>>8)&15,slot=(word>>(2*wire_id))&3;
  if((mask&(1u<<wire_id))&&slot!=((status>>4)&3))return 1;
  roster.proposal=word;wire_send((uint16_t)word);return 1;
 }
 if(type==ROSTER_COMMIT&&roster.probe&&roster.proposal==(word^0x1000u)){
  unsigned members=(word>>8)&15,map=word&255;
  if(members!=wire_members||map!=roster.map){roster.revision++;roster.dirty=1;}
  wire_members=members;roster.map=map;link_diag.members=members;
  wire_send((uint16_t)word);return 1;
 }
 if(roster.probe){
  /* Restore the acknowledgement that was pending before the out-of-band
   * handshake. In particular an already-applied COMMIT must not be replayed. */
  wire_send((uint16_t)roster.resume);roster.probe=roster.proposal=0;
  sampled=audio_game_clock();elapsed=0;deadline=WIRE_WAIT_TICKS+5u*16384u;
 }
 return 0;
}
static void roster_map(uint16_t in[4]){
 uint16_t raw[4];for(unsigned i=0;i<4;i++)raw[i]=in[i];
 for(unsigned i=0;i<4;i++)in[i]=(wire_members&(1u<<i))?raw[(roster.map>>(i*2))&3]:0xffff;
}
static int host_transfer_raw(uint16_t out,uint16_t in[4]);
static int roster_echo(unsigned word,unsigned members,unsigned map){
 if(members==1){
  /* No remaining client can hold the host's single-player continuation. */
  uint16_t in[4];start_deadline(1024);(void)host_transfer_raw((uint16_t)word,in);return 1;
 }
 start_deadline(ROSTER_WINDOW);
 for(;;){
  uint16_t in[4];if(!host_transfer_raw((uint16_t)word,in))return 0;
  unsigned yes=1;
  for(unsigned i=1;i<4;i++)if((members&(1u<<i))&&in[(map>>(2*i))&3]!=word)yes=0;
  if(yes)return 1;
 }
}
static int roster_recover(unsigned wanted){
 unsigned old_phase=link_diag.phase,original=wire_members,original_map=roster.map;
 roster.busy=1;diag_phase(LP_ROSTER);
 /* Abandon only a stuck serial transfer, never a live game transaction. */
 REG(0x04000128)=0x6003;REG(0x04000202)=0x80;wire_have_done=0;
 for(unsigned attempt=0;attempt<4;attempt++){
  unsigned allowed=wire_members&wanted,found=1,map=0,counts[4]={0};
  unsigned probe=ROSTER_PROBE|((++roster.nonce&1023u)<<2);
  start_deadline(ROSTER_WINDOW);
  for(;;){
   uint16_t in[4];if(!host_transfer_raw((uint16_t)probe,in))break;
   for(unsigned slot=1;slot<4;slot++){
    unsigned id=in[slot]&3;
    if(!id||!(allowed&(1u<<id))||(in[slot]&~3u)!=probe)continue;
    if((found&(1u<<id))&&((map>>(id*2))&3)!=slot){counts[id]=0;found&=~(1u<<id);continue;}
    map=(map&~(3u<<(id*2)))|(slot<<(id*2));
    if(++counts[id]>=2)found|=1u<<id;
   }
   if(found==allowed)break;
  }
  if(found==original&&map==original_map){roster.busy=0;diag_phase(old_phase);return 0;}
  unsigned payload=(found<<8)|map;
  if(!roster_echo(ROSTER_PROPOSE|payload,found,map))continue;
  /* Shrink monotonically even if another client vanishes during COMMIT. */
  if(found!=wire_members||map!=roster.map){roster.revision++;roster.dirty=1;}
  wire_members=found;roster.map=map;link_diag.members=found;
  if(!roster_echo(ROSTER_COMMIT|payload,found,map))continue;
  roster.busy=0;diag_phase(old_phase);start_deadline(WIRE_WAIT_TICKS);return 1;
 }
 roster.busy=0;diag_phase(old_phase);return 0;
}
void coop_wire_recovery(unsigned enabled){roster.enabled=enabled;}
unsigned coop_wire_members(void){return wire_members;}
int coop_wire_remove(unsigned removed){
 if(removed&1)return link_diag_fault(LE_TOPOLOGY,0,removed);
 unsigned wanted=wire_members&~removed;if(wanted==wire_members)return 1;
 if(!wire_id)return roster_recover(wanted);
 start_deadline(WIRE_WAIT_TICKS);
 while((wire_members&removed)==removed){
  uint16_t in[4];if(expired())return link_diag_fault(LE_RECEIVE_TIMEOUT,wanted,wire_members);
  (void)receive_sample(in);
 }
 return 1;
}
/* A departed console may stay plugged in while displaying its save/menu UI.
 * Multiplayer idle keeps SD ready; GPIO/disabled SIO could block other peers. */
void coop_wire_park(void){
 roster.enabled=0;REG(0x04000134)=0;REG(0x04000128)=0x2003;
 REG(0x0400012a)=0xffff;REG(0x04000202)=0x80;
}
