#include "audio.h"
#include "coop.h"
#include "item_effect_cache.h"
#include "game.h"
#include "items.h"
#include "../generated/combat_assets.h"
#include "../generated/butcher_assets.h"
#include "../generated/bow_assets.h"
#include "../generated/item_audio.h"
#include "../generated/levelup.h"
#include "../generated/spell_assets.h"
#include "../generated/spell_expansion_audio.h"
#include "../generated/monster_audio.h"
#include "../generated/monsters_stage66.h"
#include "../generated/monster71_audio.h"
#include "../generated/quest_audio.h"
#include "../generated/hazard_audio.h"
#include "../generated/fountain_audio.h"
#include "../generated/ending_audio.h"
#include "../generated/menu_audio.h"
#include "../generated/greeting_audio.h"
#include "../generated/entry_cow_audio.h"
#include "../generated/boss_intro_audio.h"
#include "../generated/hero_error_audio.h"
#include "../generated/audio_6k.h"
#include "../generated/hero_death_audio.h"
#include "../generated/item_grab_audio.h"
#include "../generated/music_regions_packed.h"
#ifndef R16
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#endif
typedef struct {const uint8_t *data;unsigned length;} Sample;
#define SAMPLE(n) {asset_audio_##n,AUDIO_##n##_LENGTH}
/* Names and lengths generated from original MPQ WAVs. */
static const Sample music[]={
 {asset_audio_music_town_packed,AUDIO_TRACK_TOWN_SAMPLES}, /* Lossless packed town stream below. */
 {asset_audio_music_dungeon_packed,AUDIO_TRACK_DUNGEON_SAMPLES},
 {asset_audio_music_menu_adpcm,AUDIO_TRACK_MENU_SAMPLES},
 {asset_audio_music_dlvlb_packed,AUDIO_TRACK_CATACOMBS_SAMPLES},
 {asset_audio_music_dlvlc_packed,AUDIO_TRACK_CAVES_SAMPLES},
 {asset_audio_music_dlvld_packed,AUDIO_TRACK_HELL_SAMPLES}
};
/* Complete native ending tracks, non-looping. IMA4 input is decoded into the
 * existing music DMA buffer; all6144Hz sources play at repeat1. */
static const Sample ending_music[]={
 {asset_audio_ending_sorcerer,ENDING_AUDIO_SORCERER_OUTPUT_SAMPLES},
 {asset_audio_ending_common,ENDING_AUDIO_COMMON_OUTPUT_SAMPLES}
};
static volatile struct {int16_t predictor;uint8_t index,cow_reply;} ending_decoder;
static const Sample effects[]={AUDIO_EFFECT_SAMPLES,HERO_DEATH_AUDIO_SAMPLES,ITEM_GRAB_AUDIO_SAMPLE};
enum { ITEM_GRAB_EFFECT=AUDIO_EFFECT_COUNT+3 };
enum { BOSS_INTRO_EFFECT_BASE=47+MONSTER_AUDIO_COUNT+ITEM_AUDIO_EXTRA_COUNT+HAZARD_AUDIO_COUNT,
       HERO_ERROR_EFFECT_BASE=BOSS_INTRO_EFFECT_BASE+BOSS_INTRO_AUDIO_COUNT,
       FOUNTAIN_EFFECT=HERO_ERROR_EFFECT_BASE+HERO_ERROR_AUDIO_COUNT };
_Static_assert(FOUNTAIN_EFFECT==175&&sizeof(effects)/sizeof(effects[0])==AUDIO_EFFECT_COUNT+4,"stable original IDs; new effects append");
_Static_assert(BOSS_INTRO_AUDIO_COUNT==BOSS_INTRO_COUNT,"boss gameplay and audio IDs must agree");
unsigned audio_music_enabled=1,audio_effects_enabled=1;
unsigned audio_music_volume=AUDIO_MUSIC_VOLUME_DEFAULT;
unsigned audio_effects_volume=AUDIO_EFFECTS_VOLUME_DEFAULT;
static volatile unsigned active;
static unsigned track_id,music_started,effect_priority;
/* Two 256-sample blocks plus a mirrored block after them. The mirror keeps
 * DMA1 in valid memory while its block-boundary IRQ is delayed by DMA3.
 * Timer3 supplies one IRQ per256 samples (about24Hz), not one IRQ per FIFO burst.
 * Only256 samples are scaled each time. No new timer or effect mixer buffer. */
static union {int8_t music[768];ItemEffectCache cache[4];} audio_workspace __attribute__((aligned(4)));
_Static_assert(sizeof(ItemEffectCache)*4<=768,"co-op caches fit inactive music buffer");
#define music_buffer audio_workspace.music
ItemEffectCache *audio_item_cache(void){return audio_workspace.cache;}
static unsigned music_next;
static volatile unsigned effect_started,effect_length;
static volatile unsigned effect_id,item_id,item_started,item_length,mixed_until;
static volatile unsigned effect_dma;
static volatile unsigned sample_epoch;
static uint32_t sound_rng=0x1837d5a1u;
static unsigned hero_error_next;
static uint8_t item_pending[2];
static unsigned speech_id,speech_started,speech_decoded;
static volatile unsigned speech_length;
static const GreetingAudioClip *speech_clip;
static volatile struct {int16_t predictor;uint8_t index,cow_clicks;} speech_decoder;
/* Cow interaction counters occupy existing decoder alignment bytes. */
#define cow_clicks speech_decoder.cow_clicks
#define cow_reply ending_decoder.cow_reply
volatile unsigned audio_last_effect,audio_effect_count,audio_music_loops;
static unsigned sound_random(void){sound_rng^=sound_rng<<13;sound_rng^=sound_rng>>17;sound_rng^=sound_rng<<5;return sound_rng;}
static struct {uint32_t started;uint16_t base,active;} gold_clock;
static unsigned sound_control(unsigned value){
 if(gold_clock.active)value&=~0x4000u;
 /* Direct Sound A gains only support50/100%. Use100% and PCM gain15.6..100%;
  * the default78% is20% above Stage46's65%. FIFO B retains its hardware gain.
  * Muting only changes A routing, so toggles never desynchronise its DMA. */
 return (value|4)&(audio_music_enabled&&audio_music_volume&&!coop_active?~0u:~0x300u);
}
static __attribute__((section(".iwram.coop"),noinline)) unsigned clock_samples(void){
 unsigned ime=R16(0x04000208),hi,lo,pending;R16(0x04000208)=0;
 do{hi=R16(0x0400010c);lo=R16(0x04000108)&15;pending=R16(0x04000202)&64;}while(hi!=R16(0x0400010c));
 unsigned now=sample_epoch+(pending?256:0)+((hi&15)<<4)+lo;
 R16(0x04000208)=(uint16_t)ime;return now;
}
#include "../generated/audio_gains.h"
/* Reuse the linker's explicitly retained fast-code section. Keep only this
 * bounded mapping loop in IWRAM; stream/wrap/IRQ control stays in ROM. */

/* Process a complete bounded PCM block. The caller separately anchors the
 * clock immediately after the first FIFO word. Sources may be unaligned.
 * One surviving voice also uses fast RAM after combat interrupts its DMA. */
static __attribute__((section(".iwram.coop"),noinline)) void effects_pcm_words(const int8_t *a,const int8_t *b,unsigned count){
 if(!b){
  for(unsigned n=0;n<count;n+=4){
   const uint8_t *p=(const uint8_t*)a+n;
   R32(0x040000a4)=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
  }
 }else for(unsigned n=0;n<count;n+=4){
  uint32_t word=0;
#if defined(__clang__)
#pragma clang loop unroll(disable)
#endif
  for(unsigned shift=0;shift<32;shift+=8){
   int sample=*a++ + *b++;if(sample>127)sample=127;if(sample< -128)sample=-128;
   word|=(uint32_t)(uint8_t)sample<<shift;
  }
  R32(0x040000a4)=word;
 }
}
#include "music_packed.inc"
static __attribute__((always_inline)) inline void ima_step(unsigned code,int *predictor,int *index){
 static const uint16_t steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
 static const int8_t adjustments[8]={-1,-1,-1,-1,2,4,6,8};
 unsigned step=steps[*index],change=step>>3;
 if(code&4)change+=step;if(code&2)change+=step>>1;if(code&1)change+=step>>2;
 *predictor+=code&8?-(int)change:(int)change;
 if(*predictor<-32768)*predictor=-32768;if(*predictor>32767)*predictor=32767;
 *index+=adjustments[code&7];if(*index<0)*index=0;if(*index>88)*index=88;
}
static void adpcm_music_fill(int8_t *dest){
 const Sample *clip=track_id==2?&music[2]:&ending_music[track_id-AUDIO_MUSIC_TRACKS];
 unsigned position=music_next,left=256,repeat=track_id==2?MENU_AUDIO_REPEAT:ENDING_AUDIO_REPEAT;
 const int8_t *gain=audio_music_volume?music_gain[audio_music_volume-1]:0;
 int predictor=ending_decoder.predictor,index=ending_decoder.index;
 while(left){
 unsigned count=clip->length-position;if(count>left)count=left;
 for(unsigned n=0;n<count/repeat;n++){
  unsigned sample=position/repeat+n;
  unsigned code=(clip->data[sample>>1]>>((sample&1)*4))&15;
  ima_step(code,&predictor,&index);
  int8_t value=gain?gain[(uint8_t)(predictor>>8)]:0;
  for(unsigned r=0;r<repeat;r++)dest[n*repeat+r]=value;
 }
 dest+=count;left-=count;position+=count;
 if(track_id==2&&position==clip->length){
  /* The title/main-menu track loops from its original initial IMA state,
   * including a partial final block. Ending clips remain non-looping. */
  position=0;predictor=index=0;audio_music_loops++;
 }else{
  for(unsigned n=0;n<left;n++)dest[n]=0;
  break;
 }
 }
 ending_decoder.predictor=(int16_t)predictor;ending_decoder.index=(uint8_t)index;
 /* Exact sample length, not padded nibble length. Once exhausted, keep DMA
  * fed with silence until the presentation calls audio_stop(). */
 music_next=position;
}
static void music_fill(int8_t *dest){
 if(track_id==2||track_id>=AUDIO_MUSIC_TRACKS){adpcm_music_fill(dest);return;}
 const uint8_t *source=music[track_id].data;
 unsigned position=music_next,length=music[track_id].length,volume=audio_music_volume;
 if(!volume){
  for(unsigned i=0;i<64;i++)((uint32_t*)dest)[i]=0;
  position+=256;if(position>=length){position-=length;audio_music_loops++;}
  music_next=position;return;
 }
 const int8_t *gain=music_gain[volume-1];
 unsigned left=256;
 while(left){
  unsigned count=length-position;if(count>left)count=left;
  music_packed_stream_map(dest,source,position,gain,count);
  dest+=count;left-=count;position+=count;
  if(position==length){position=0;audio_music_loops++;}
 }
 music_next=position;
}
static __attribute__((noinline)) void music_feed(void){
 /* FIFO keeps a4-byte output latch separate from its32-byte ring. With
  * empty startup, sample0 is latched at tick2; refill bursts start at3,19,
  *35,... . Thus source256+16 (not+32) is next at tick256. Preserve the
  * FIFO/latch and resume at the first untransferred16-byte burst. */
 /* Freeze the shared sample timer during the DMA source handoff so a FIFO
  * request cannot race the clock read. Restore the current counter first,
  * then the normal reload; this retains the partially elapsed sample. */
 while(R16(0x04000104)>0xff80){} /* Leave enough time to restore reload. */
 unsigned timer=R16(0x04000106);R16(0x04000106)=0;
 unsigned counter=R16(0x04000104);R32(0x040000c4)=0;
 unsigned now=clock_samples(),block=(now>>8)&1,offset=(((now&255)+13)&~15u)+16;
 R32(0x040000bc)=(uint32_t)(uintptr_t)(music_buffer+block*256+offset);
 R32(0x040000c4)=0xb6400004;
 R16(0x04000104)=(uint16_t)counter;R16(0x04000106)=(uint16_t)timer;R16(0x04000104)=AUDIO_TIMER_RELOAD;
 music_fill(music_buffer+(block^1)*256);
 if(block){
  for(unsigned i=0;i<64;i++)((uint32_t*)(music_buffer+512))[i]=((uint32_t*)music_buffer)[i];
 }
}
/* FIFO A uses scaled ping-pong DMA music. At default sound5, a newly started
 * single effect uses DMA2 directly from its aligned ROM PCM. Other sound
 * levels use the existing bounded mixer and a ROM gain LUT, without a buffer.
 * Every packaged effect has >=1024 bytes
 * of silent guard; the independent VBlank IRQ stops it within one VBlank
 * (at most275 samples, plus32 bytes FIFO prefetch), even during a slow draw.
 * Combat+item overlap keeps the bounded CPU mixer: DMA cannot add voices.
 * An unaligned resumed voice also stays in that path to preserve its exact
 * sample position. No streaming RAM allocation or ring-wrap deadline is
 * needed. Like music DMA, this requires interrupts to remain enabled during
 * playback; long loading transitions call audio_stop before loading work.
 * Timer2/3 retain a uint32 sample clock, including wrap. Timer3 now reloads
 * every256 samples; IRQs must not be masked for12ms during playback. */
#include "audio_gold.inc"
static void effects_feed(unsigned now){
 if(speech_length&&now-speech_started>=speech_length)speech_length=0;
 if(effect_length&&now-effect_started>=effect_length)effect_length=0;
 if(item_length&&now-item_started>=item_length)item_length=0;
 if(gold_clock.active&&(!item_length||item_id!=26||speech_length)){
  gold_clock_stop();R32(0x040000d0)=0;effect_dma=0;R16(0x04000082)=sound_control(0xf708);mixed_until=now;
 }
 if(!effect_length&&!item_length&&!speech_length){R32(0x040000d0)=0;effect_dma=0;R16(0x04000200)&=(uint16_t)~32;R16(0x04000082)=sound_control(0xf708);return;}
 if(effect_dma)return;
 /* Start at offset zero only. Later transitions need to retain sub-word
  * sample alignment and already mixed FIFO contents, so remain CPU-fed. */
 unsigned voice=effect_length&&!item_length&&now==effect_started?1:!effect_length&&item_length&&now==item_started?2:0;
 if(!speech_length&&voice&&audio_effects_volume==AUDIO_EFFECTS_VOLUME_DEFAULT){
  R16(0x04000200)&=(uint16_t)~32;
  R32(0x040000c8)=(uint32_t)(uintptr_t)effects[voice==1?effect_id:item_id].data;
  R32(0x040000cc)=0x040000a4;effect_dma=voice;R32(0x040000d0)=0xb6400004;
  return;
 }
 if(gold_clock.active){gold_feed(now);return;}
 int ahead=(int)(mixed_until-now);
 if(ahead<0||ahead>32){R16(0x04000082)=sound_control(0xf708);mixed_until=now;ahead=0;}
 /* Fill whole FIFO words, including partial 16-sample blocks. This leaves
  * at least12 samples of headroom at the next Timer2 IRQ at any start phase. */
 if(ahead>28)return;
 unsigned count=(32-(unsigned)ahead)&~3u;
 const int8_t *combat=(const int8_t*)effects[effect_id].data,*item=(const int8_t*)effects[item_id].data;
 const int8_t *gain=effects_gain[audio_effects_volume-1]+256;
 unsigned combat_length=effect_length,item_size=item_length,combat_offset=mixed_until-effect_started,item_offset=mixed_until-item_started;
 int predictor=speech_decoder.predictor,index=speech_decoder.index;
 for(unsigned n=0;n<count;n+=4){
  if(!speech_length&&audio_effects_volume==AUDIO_EFFECTS_VOLUME_DEFAULT){
   unsigned left=n?count-n:4;
   if((combat_offset>=combat_length||combat_length-combat_offset>=left)&&(item_offset>=item_size||item_size-item_offset>=left)){
    const int8_t *a=combat_offset<combat_length?combat+combat_offset:0,*b=item_offset<item_size?item+item_offset:0;
    if(a||b){
     effects_pcm_words(a?a:b,a?b:0,left);if(n)break;
     combat_offset+=4;item_offset+=4;goto word_written;
    }
   }
  }
  uint32_t word=0;
  /* Most combat IRQs have only one voice. Copy a complete PCM word without
   * four signed mixes/clamps. Byte loads intentionally support every source
   * alignment; never read past a short tail or a not-yet-started voice. */
  const uint8_t *single=0;
  if(item_offset>=item_size&&combat_offset<combat_length&&combat_length-combat_offset>=4)single=(const uint8_t*)combat+combat_offset;
  else if(combat_offset>=combat_length&&item_offset<item_size&&item_size-item_offset>=4)single=(const uint8_t*)item+item_offset;
  if(!speech_length&&single&&audio_effects_volume==AUDIO_EFFECTS_VOLUME_DEFAULT){
   word=(uint32_t)single[0]|((uint32_t)single[1]<<8)|((uint32_t)single[2]<<16)|((uint32_t)single[3]<<24);
   combat_offset+=4;item_offset+=4;
  }else for(unsigned b=0;b<4;b++){
   int sample=0;
   if(speech_length){
    unsigned position=mixed_until+n+b-speech_started;
    if(position<speech_length){
     unsigned input=position/GREETING_AUDIO_REPEAT;
     /* Catch up after a delayed IRQ without replaying stale voice samples.
      * Ordinary service decodes eight nibbles per16 output samples. */
     while(speech_decoded<=input){
      unsigned at=speech_decoded++,code=(speech_clip->data[at>>1]>>((at&1)*4))&15;
      ima_step(code,&predictor,&index);
     }
     sample=predictor>>8;
    }
   }else{
    if(combat_offset<combat_length)sample=combat[combat_offset];
    if(item_offset<item_size)sample+=item[item_offset];
   }
   combat_offset++;item_offset++;
   sample=gain[sample];
   word|=(uint32_t)(uint8_t)sample<<(b*8);
  }
  R32(0x040000a4)=word;
word_written:
  /* The FIFO can underrun while the first new word is being calculated.
   * Anchor its occupancy after that write, then skip elapsed source samples.
   * The estimate can now only overstate occupancy, never overfill the FIFO. */
  if(!n){unsigned written=clock_samples();if((int)(written-mixed_until)>0){unsigned elapsed=written-mixed_until;mixed_until=written;combat_offset+=elapsed;item_offset+=elapsed;}}
 }
 speech_decoder.predictor=(int16_t)predictor;speech_decoder.index=(uint8_t)index;
 mixed_until+=count;
 /* Silent scenes keep only VBlank and the64Hz music-block IRQ. */
 if(!(R16(0x04000200)&32)){R16(0x04000202)=32;R16(0x04000200)|=32;}
}
/* BIOS IRQ trampoline saves caller registers; ordinary ARM C returns via LR.
 * Never queue or restart voices here; only feed their bounded sample ranges. */
void lighting_hardware_vblank_irq(void);
void info_stream_irq(unsigned pending);
__attribute__((section(".iwram.coop"),noinline)) void audio_vblank_irq(void){
 unsigned pending=R16(0x04000200)&R16(0x04000202);
 if(pending&64)sample_epoch+=256;
 R16(0x04000202)=(uint16_t)pending;
 if(pending&5)info_stream_irq(pending);
 if(pending&1)lighting_hardware_vblank_irq();
 if(active&&(pending&33))effects_feed(clock_samples());
 /* Refill mixed SFX first: its FIFO then has nearly32 samples of headroom
  * while256 BGM samples are mapped into the retired DMA block. */
 if(active&&(pending&64)&&!coop_active)music_feed();
 R16(0x03007ff8)|=(uint16_t)pending;
}
void audio_stop(void){
 cow_reply&=3;
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;gold_clock_stop();
 R32(0x040000c4)=0;R32(0x040000d0)=0;R16(0x04000106)=0;
 R16(0x0400010a)=0;R16(0x0400010e)=0;R16(0x04000084)=0;active=0;effect_length=0;speech_length=0;
 effect_dma=0;item_length=0;item_pending[0]=item_pending[1]=0;hero_error_next=0;
 R16(0x04000200)&=(uint16_t)~96;R16(0x04000202)=96;R16(0x04000208)=(uint16_t)ime;
}
static void music_play(void){
 R32(0x040000c4)=0;R16(0x04000082)=sound_control(0x7f08);
 music_next=0;music_fill(music_buffer);music_fill(music_buffer+256);
 for(unsigned i=0;i<64;i++)((uint32_t*)(music_buffer+512))[i]=((uint32_t*)music_buffer)[i];
 R32(0x040000bc)=(uint32_t)(uintptr_t)music_buffer;R32(0x040000c0)=0x040000a0;R32(0x040000c4)=0xb6400004;
 music_started=clock_samples();
}
static void audio_begin(unsigned track){
 audio_stop();
 R16(0x04000208)=0;
 R32(0x03007ffc)=(uint32_t)(uintptr_t)audio_vblank_irq;
 R16(0x04000202)=97;R16(0x04000004)|=8;R16(0x04000200)|=65;
 track_id=track;ending_decoder.predictor=0;ending_decoder.index=0;
 R16(0x04000084)=0x80;R16(0x04000080)=0;R16(0x04000082)=sound_control(0xff08);
 sample_epoch=0;R16(0x04000108)=0xfff0;R16(0x0400010c)=0xfff0;R16(0x0400010e)=0xc4;R16(0x0400010a)=0xc4;
 R16(0x04000104)=AUDIO_TIMER_RELOAD;active=1;item_effect_cache_reset();if(!coop_active)music_play();R16(0x04000106)=0x80;R16(0x04000208)=1;
}
void audio_start(unsigned track){audio_begin(track<AUDIO_MUSIC_TRACKS?track:0);}
void audio_ending_start(unsigned track){audio_begin(AUDIO_MUSIC_TRACKS+(track?1:0));}
unsigned audio_ending_position(void){
 if(!active||track_id<AUDIO_MUSIC_TRACKS)return 0;
 /* The first timer tick is silent; sample0 is heard at tick2. The decoded
  * cursor runs ahead by up to512 samples and cannot signal audible EOF. */
 unsigned now=clock_samples()-music_started,played=now?now-1:0;
 unsigned length=ending_music[track_id-AUDIO_MUSIC_TRACKS].length;
 return played<length?played:length;
}
int audio_ending_busy(void){return active&&track_id>=AUDIO_MUSIC_TRACKS&&audio_ending_position()<ending_music[track_id-AUDIO_MUSIC_TRACKS].length;}
static void effect_play(unsigned id,unsigned priority){
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 R32(0x040000d0)=0;effect_dma=0;R16(0x04000082)=sound_control(0xf708);
 effect_id=id;effect_started=clock_samples();effect_length=effects[id].length;effect_priority=priority;
 mixed_until=effect_clock(effect_started);effects_feed(effect_started);
 audio_last_effect=id;audio_effect_count++;R16(0x04000208)=(uint16_t)ime;
}
void audio_update(void){
 if(!active)return;unsigned ime=R16(0x04000208);R16(0x04000208)=0;unsigned now=clock_samples();
 effects_feed(now);
 if(audio_effects_enabled&&!item_length&&!speech_length&&(!coop_active||!effect_length||effect_priority<6)){
  unsigned type=item_pending[1]?1:0;
  if(item_pending[type]){
   if(coop_active)effect_length=0;
   item_id=item_pending[type]-1;item_pending[type]=0;item_started=now;item_length=effects[item_id].length;
   if(item_id==26)gold_clock_start(now);
   audio_last_effect=item_id;audio_effect_count++;R32(0x040000d0)=0;effect_dma=0;R16(0x04000082)=sound_control(0xf708);mixed_until=effect_clock(now);effects_feed(now);
  }
 }
 R16(0x04000208)=(uint16_t)ime;
 if((cow_reply>>2)&&!speech_length){unsigned voice=256+(cow_reply>>2);cow_reply&=3;audio_speech_start(voice,0,0);}
}
int audio_finale_busy(void){
 /* The display can wait after the last world tick without replaying a cue.
  * Expire its sample first; queued lower-priority sounds cannot extend it. */
 audio_update();return active&&effect_length&&effect_priority==7;
}
void audio_effect(unsigned id,unsigned priority){
 if(!active||!audio_effects_enabled||speech_length||id>=sizeof(effects)/sizeof(effects[0]))return;
 audio_update();if(effect_length&&priority<effect_priority)return;
 if(coop_active&&item_length){if(priority<6)return;unsigned ime=R16(0x04000208);R16(0x04000208)=0;item_length=0;gold_clock_stop();R16(0x04000208)=(uint16_t)ime;}
 effect_play(id,priority);
}
void audio_queue_item(unsigned kind,unsigned pickup){
 if(!active||!audio_effects_enabled||speech_length||kind>ITEM_SOUND_GRAB||pickup>1)return;
 /* Item sounds have their own voice; nearby loot still coalesces by type. */
 item_pending[pickup]=(kind==ITEM_SOUND_GRAB?ITEM_GRAB_EFFECT:item_audio_ids[pickup][kind])+1;
 audio_update();
}
void audio_boss_intro(unsigned id){
 if(!active||!audio_effects_enabled||id>=BOSS_INTRO_AUDIO_COUNT)return;
 audio_speech_stop();audio_update();
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 /* Reuse the independent item voice so a long introduction cannot suppress
  * combat through its priority. Queued loot keeps coalescing until it ends. */
 if(coop_active)effect_length=0;
 item_id=BOSS_INTRO_EFFECT_BASE+id;item_started=clock_samples();item_length=effects[item_id].length;
 R32(0x040000d0)=0;effect_dma=0;R16(0x04000082)=sound_control(0xf708);
 mixed_until=effect_clock(item_started);effects_feed(item_started);
 audio_last_effect=item_id;audio_effect_count++;R16(0x04000208)=(uint16_t)ime;
}
int audio_boss_busy(void){
 audio_update();return active&&item_length&&item_id>=BOSS_INTRO_EFFECT_BASE&&item_id<BOSS_INTRO_EFFECT_BASE+BOSS_INTRO_AUDIO_COUNT;
}
void audio_hero_error(unsigned cls,unsigned reason){
 if(!active||!audio_effects_enabled||cls>2||reason>1)return;
 audio_update();if(speech_length||item_length||(coop_active&&effect_length&&effect_priority>=6))return;
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;unsigned now=clock_samples();
 if(hero_error_next&&(int)(now-hero_error_next)<0){R16(0x04000208)=(uint16_t)ime;return;}
 unsigned variant=0;
 if(reason){
  variant=sound_random()%3;
  /* Native AutoGetItem chooses class14+random3. Warrior14's RndSFX
   * applies a second random3; its14B/14C IDs do not (weights1:4:4). */
  if(!cls&&!variant)variant=sound_random()%3;
  variant++;
 }
 hero_error_next=now+2*AUDIO_SAMPLE_RATE; /* Two seconds; active speech also blocks replay. */
 if(coop_active)effect_length=0;
 item_id=HERO_ERROR_EFFECT_BASE+cls*4+variant;item_started=now;item_length=effects[item_id].length;
 R32(0x040000d0)=0;effect_dma=0;R16(0x04000082)=sound_control(0xf708);
 mixed_until=effect_clock(now);effects_feed(now);audio_last_effect=item_id;audio_effect_count++;
 R16(0x04000208)=(uint16_t)ime;
}
void audio_speech_stop(void){
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 if(speech_length){
  speech_length=0;R32(0x040000d0)=0;effect_dma=0;
  R16(0x04000082)=sound_control(0xf708);R16(0x04000200)&=(uint16_t)~32;
 }
 R16(0x04000208)=(uint16_t)ime;
}
int audio_speech_start(unsigned voice,void *workspace,unsigned bytes){
 (void)workspace;(void)bytes;
 if(!active||!audio_effects_enabled)return 0;
 if(voice<256&&track_id!=0)return 0;
 const GreetingAudioClip *clip=0;
 for(unsigned i=0;i<GREETING_AUDIO_COUNT;i++)if(greeting_audio[i].voice==voice)clip=&greeting_audio[i];
 for(unsigned i=0;i<ENTRY_COW_AUDIO_COUNT;i++)if(entry_cow_audio[i].voice==voice)clip=&entry_cow_audio[i];
 if(!clip)return 0;
 if(voice<256){cow_clicks=0;cow_reply=0;}
 audio_speech_stop();
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 R32(0x040000d0)=0;effect_dma=0;effect_length=item_length=0;item_pending[0]=item_pending[1]=0;
 R16(0x04000082)=sound_control(0xf708);
 speech_clip=clip;speech_id=voice;speech_length=clip->samples*GREETING_AUDIO_REPEAT;
 speech_decoded=0;speech_decoder.predictor=0;speech_decoder.index=0;
 speech_started=clock_samples();mixed_until=effect_clock(speech_started);effects_feed(speech_started);
 R16(0x04000208)=(uint16_t)ime;return 1;
}
void audio_speech_poll(void){if(speech_length)audio_update();}
int audio_speech_active(void){return active&&speech_length&&clock_samples()-speech_started<speech_length;}
void audio_set_music_volume(unsigned level){
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 audio_music_volume=level<AUDIO_VOLUME_MIN?AUDIO_VOLUME_MIN:level>AUDIO_MUSIC_VOLUME_MAX?AUDIO_MUSIC_VOLUME_MAX:level;
 if(active)R16(0x04000082)=sound_control((R16(0x04000082)&~0x300u)|0x300u);
 R16(0x04000208)=(uint16_t)ime;
}
void audio_set_effects_volume(unsigned level){
 level=level<AUDIO_VOLUME_MIN?AUDIO_VOLUME_MIN:level>AUDIO_EFFECTS_VOLUME_MAX?AUDIO_EFFECTS_VOLUME_MAX:level;
 if(level==audio_effects_volume)return;
 unsigned ime=R16(0x04000208);R16(0x04000208)=0;
 audio_effects_volume=level;
 /* A greeting's predictor follows already queued PCM. Preserve that queue
  * on gain changes; the new Sound level settles within32 output samples. */
 if(active&&!speech_length&&(effect_length||item_length)){
  unsigned now=clock_samples();R32(0x040000d0)=0;effect_dma=0;
  R16(0x04000082)=sound_control(0xf708);mixed_until=effect_clock(now);effects_feed(now);
 }
 R16(0x04000208)=(uint16_t)ime;
}
void audio_monster(unsigned kind,unsigned action){
 if(kind>=MONSTER_TYPES||action>2)return;
 audio_effect(monster71_audio_ids[kind][action][sound_random()&1],kind==MONSTER_DIABLO&&action==2?7:action==2?5:action?3:1);
}

void audio_hero_death(unsigned cls){
 if(cls<3){audio_speech_stop();audio_effect(AUDIO_EFFECT_COUNT+cls,6);}
}

void audio_spell(unsigned spell){
 unsigned n=46+MONSTER_AUDIO_COUNT;
 unsigned id=spell==2||spell==32||spell==34?43:spell==3||spell==4||spell==18?n-2:spell==7||spell==23?n-1:42;
 audio_effect(id,4);
}

void audio_quest_complete(void){audio_effect(46+MONSTER_AUDIO_COUNT+ITEM_AUDIO_EXTRA_COUNT,6);}
void audio_hazard(unsigned type){if(type==2)audio_effect(FOUNTAIN_EFFECT,4);else if(type<HAZARD_AUDIO_COUNT)audio_effect(47+MONSTER_AUDIO_COUNT+ITEM_AUDIO_EXTRA_COUNT+type,4);}

/* Original town click sequence: fourth moo differs, then class replies cycle. */
void audio_cow(unsigned cls){
 if(!active||track_id||!audio_effects_enabled||audio_speech_active()||cls>2)return;
 unsigned voice=269;
 if(++cow_clicks==4)voice=270;
 if(cow_clicks>=8){cow_clicks=4;unsigned reply=cow_reply&3;cow_reply=((15+cls*3+reply)<<2)|((reply+1)%3);}
 audio_speech_start(voice,0,0);
}
