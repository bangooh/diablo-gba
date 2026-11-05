#pragma once
#include <stdint.h>
enum { COOP_BLOCK_BYTES=256 };
/* Small, bounded LZ10 blocks. No allocation and no persistent dictionary.
 * The source and encoded lengths are both checked on the receiving side. */
static unsigned coop_lz_pack(const uint8_t *src,unsigned bytes,uint8_t *dst,unsigned cap){
 uint16_t head[64]={0};unsigned pos=0,out=4;
 if(bytes<8||cap<4)return 0;
 dst[0]=16;dst[1]=(uint8_t)bytes;dst[2]=(uint8_t)(bytes>>8);dst[3]=0;
 while(pos<bytes){
  if(out>=cap)return 0;unsigned flags=out++;dst[flags]=0;
  for(unsigned mask=128;mask&&pos<bytes;mask>>=1){
   unsigned match=0,distance=0;
   if(pos+2<bytes){
    unsigned key=(src[pos]*17u^src[pos+1]*7u^src[pos+2])&63u,old=head[key];
    if(old){unsigned at=old-1;while(match<18&&pos+match<bytes&&src[at+match]==src[pos+match])match++;distance=pos-at;}
   }
   unsigned count=match>=3?match:1;
   if(match>=3){
    if(out+2>cap)return 0;dst[flags]|=(uint8_t)mask;
    dst[out++]=(uint8_t)(((match-3)<<4)|((distance-1)>>8));dst[out++]=(uint8_t)(distance-1);
   }else{if(out>=cap)return 0;dst[out++]=src[pos];}
   for(unsigned i=0;i<count;i++,pos++)if(pos+2<bytes)head[(src[pos]*17u^src[pos+1]*7u^src[pos+2])&63u]=(uint16_t)(pos+1);
  }
 }
 return out;
}
static int coop_lz_unpack(const uint8_t *src,unsigned length,uint8_t *dst,unsigned bytes){
 if(length<4||src[0]!=16||(src[1]|((unsigned)src[2]<<8)|((unsigned)src[3]<<16))!=bytes)return 0;
 unsigned in=4,out=0;
 while(out<bytes){
  if(in>=length)return 0;unsigned flags=src[in++];
  for(unsigned mask=128;mask&&out<bytes;mask>>=1){
   if(flags&mask){
    if(in+2>length)return 0;unsigned a=src[in++],b=src[in++],n=(a>>4)+3,d=((a&15)<<8)+b+1;
    if(d>out||n>bytes-out)return 0;while(n--){dst[out]=dst[out-d];out++;}
   }else{if(in>=length)return 0;dst[out++]=src[in++];}
  }
 }
 return in==length;
}
static uint32_t coop_block_crc(const uint8_t *data,unsigned bytes,unsigned sequence){
 uint32_t crc=~((uint32_t)sequence|((uint32_t)bytes<<16));
 for(unsigned i=0;i<bytes;i++){crc^=data[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
 return ~crc;
}
