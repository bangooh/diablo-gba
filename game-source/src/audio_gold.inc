/* Gold alone uses true8-kHz DMA on FIFO B/Timer0. During overlap the bounded
 * mixer keeps gold at8k and samples the existing6144-Hz combat voice at its
 * original pitch. Music stays on Timer1 without resampling or extra buffers.
 * Preserve the old16,384-Hz UI/game clock while Timer0 is borrowed. */
unsigned audio_game_clock(void){
 /* Gold completion may restore Timer0 in an IRQ. Snapshot the mode and its
  * counter atomically so one read cannot straddle that clock handover. */
 if(!gold_clock.active)return R16(0x04000100);
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 unsigned now=gold_clock.active?(uint16_t)(gold_clock.base+((clock_samples()-gold_clock.started)*2731u>>10)):R16(0x04000100);
 R16(0x04000208)=(uint16_t)ime;return now;
}
static void gold_clock_stop(void){
 if(!gold_clock.active)return;
 unsigned now=audio_game_clock();gold_clock.active=0;
 R16(0x04000102)=0;R16(0x04000100)=(uint16_t)now;R16(0x04000102)=0x83;
 /* While running, writing TM0D updates only its next-overflow reload.
  * Keep the restored live count, but return to a full65536-tick period.
  * Leaving `now` as reload makes elapsed time jump on every overflow. */
 R16(0x04000100)=0;
}
static void gold_clock_start(unsigned now){
 if(gold_clock.active)return;
 gold_clock.base=R16(0x04000100);gold_clock.started=now;gold_clock.active=1;
 R16(0x04000102)=0;R16(0x04000100)=65536-2097;R16(0x04000102)=0x80;
}
static unsigned effect_clock(unsigned now){return gold_clock.active?(now-gold_clock.started)*125u/96u:now;}
static void gold_feed(unsigned now){
 unsigned output=effect_clock(now);int ahead=(int)(mixed_until-output);
 if(ahead<0||ahead>32){R16(0x04000082)=sound_control(0xf708);mixed_until=output;ahead=0;}
 if(ahead>28)return;
 unsigned count=(32-(unsigned)ahead)&~3u;
 const int8_t *gold=(const int8_t*)effects[26].data,*combat=(const int8_t*)effects[effect_id].data;
 const int8_t *gain=effects_gain[audio_effects_volume-1]+256;
 for(unsigned n=0;n<count;n+=4){
  uint32_t word=0;
  for(unsigned b=0;b<4;b++){
   unsigned at=mixed_until+n+b;
   int offset=(int)(gold_clock.started+at*96u/125u-effect_started);
   int value=at<6432?gold[at]:0;
   if(effect_length&&offset>=0&&(unsigned)offset<effect_length)value+=combat[offset];
   word|=(uint32_t)(uint8_t)gain[value]<<(b*8);
  }
  R32(0x040000a4)=word;
  if(!n){unsigned written=effect_clock(clock_samples());if((int)(written-mixed_until)>0)mixed_until=written;}
 }
 mixed_until+=count;
 if(!(R16(0x04000200)&32)){R16(0x04000202)=32;R16(0x04000200)|=32;}
}
