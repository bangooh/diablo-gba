/* Lossless town PCM, with independently seekable 256-sample blocks. The
 * generated ROM stream starts with one uint32 offset per block plus an end
 * offset. Each block has a signed minimum, a bit width, then LSB-first deltas.
 * Bit7 selects delta blocks: first sample, signed16 minimum difference,
 * then255 packed differences; PCM remains byte-exact.
 * Only the caller's existing music buffer is written; no decoder state or
 * scratch survives the call. Keep this code in ROM, outside the IWRAM budget.
 * Caller supplies count <= 256 and position + count <= the track length. */
#include "../generated/music_town_packed.h"
static __attribute__((section(".text.ui"),noinline)) void music_packed_stream_map(
 int8_t *dest,const uint8_t *source,unsigned position,const int8_t *gain,unsigned count){
 const uint32_t *offsets=(const uint32_t*)source;
 while(count){
  unsigned within=position&255,part=256-within;if(part>count)part=count;
  const uint8_t *block=source+offsets[position>>8];
  unsigned base=block[0],bits=block[1];
  if(bits&128){
   bits&=127;
   int value=(int8_t)base,low=(int16_t)((unsigned)block[2]|((unsigned)block[3]<<8));
   unsigned pending=0,available=0,mask=(1u<<bits)-1;
   const uint8_t *packed=block+4;
   for(unsigned i=0;i<within+part;i++){
    if(i){
     while(available<bits){pending|=(unsigned)*packed++<<available;available+=8;}
     value+=low+(int)(pending&mask);pending>>=bits;available-=bits;
    }
    if(i>=within)dest[i-within]=gain[(uint8_t)value];
   }
  }else if(!bits){
   int8_t value=gain[base];for(unsigned i=0;i<part;i++)dest[i]=value;
  }else{
   unsigned bit=within*bits,shift=bit&7,available=8-shift,mask=(1u<<bits)-1;
   const uint8_t *packed=block+2+(bit>>3);
   unsigned pending=*packed++>>shift;
   for(unsigned i=0;i<part;i++){
    if(available<bits){pending|=(unsigned)*packed++<<available;available+=8;}
    dest[i]=gain[(base+(pending&mask))&255];
    pending>>=bits;available-=bits;
   }
  }
  dest+=part;position+=part;count-=part;
 }
}

#define music_packed_map(dest,position,gain,count) music_packed_stream_map(dest,asset_audio_music_town_packed,position,gain,count)
