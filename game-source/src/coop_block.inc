/* Host broadcast, with alternating data tags and an acknowledged CRC verdict.
 * A whole block remains staged until every peer agrees. The existing completed
 * SIO sampling, 4-tick protection gap and 30-second deadlines are retained.
 * New lobby protocol D107 rejects older peers before starting this stream. */
#include "coop_codec.h"
#ifndef COOP_BLOCK_LZ
#define COOP_BLOCK_LZ 1
#endif
static unsigned block_word(const uint8_t *data,unsigned bytes,unsigned index){
 unsigned bit=index*12,at=bit>>3,shift=bit&7,value=data[at];
 if(at+1<bytes)value|=(unsigned)data[at+1]<<8;
 return ((value>>shift)&4095)|(index&1?0xa000:0x9000);
}
static void block_put(uint8_t *data,unsigned bytes,unsigned index,unsigned word){
 unsigned bit=index*12,at=bit>>3,shift=bit&7;
 if(!shift){data[at]=(uint8_t)word;if(at+1<bytes)data[at+1]=(uint8_t)((data[at+1]&0xf0)|((word>>8)&15));}
 else{data[at]=(uint8_t)((data[at]&15)|(word<<4));if(at+1<bytes)data[at+1]=(uint8_t)(word>>4);}
}
int coop_wire_block(void *buffer,unsigned bytes){
 if(!bytes||bytes>COOP_BLOCK_BYTES)return link_diag_fault(LE_BLOCK_HEADER,COOP_BLOCK_BYTES,bytes);
 if(wire_members==1)return 1;
 uint8_t packed[COOP_BLOCK_BYTES+4],decoded[COOP_BLOCK_BYTES];uint8_t *data=buffer;
 unsigned length=bytes,compressed=0;
 if(!wire_id){
  unsigned n=COOP_BLOCK_LZ?coop_lz_pack(data,bytes,packed+4,bytes):0;
  if(n&&n+8<=bytes){length=n;compressed=1;}else for(unsigned i=0;i<bytes;i++)packed[4+i]=data[i];
 }
 for(unsigned attempt=0;attempt<4;attempt++){
  uint16_t values[4];
  if(!coop_wire_frame(wire_id?0:(uint16_t)(length|(compressed<<9)),values,0))return 0;
  unsigned size=values[0]&511,codec=(values[0]>>9)&1,seq=wire_seq&0x7ff;
  if((values[0]&~1023u)||!size||size>bytes||(!codec&&size!=bytes))return link_diag_fault(LE_BLOCK_HEADER,bytes,values[0]);
  unsigned total=size+4,words=(total*8+11)/12,stop=0xd000|seq;
  diag_phase(LP_BLOCK_DATA);
  if(!wire_id){
   uint32_t crc=coop_block_crc(data,bytes,seq);for(unsigned i=0;i<4;i++)packed[i]=(uint8_t)(crc>>(8*i));
   for(unsigned i=0;i<words;i++)if(!host_echo((uint16_t)block_word(packed,total,i)))return 0;
   diag_phase(LP_BLOCK_CRC);unsigned good=0;
   for(;;){
    uint16_t in[4];if(!host_transfer((uint16_t)stop,in))return 0;unsigned ready=1;good=1;
    for(unsigned i=1;i<4;i++)if(wire_members&(1u<<i)){
     if(in[i]!=(0xb000|seq)&&in[i]!=(0xc000|seq))ready=0;
     if(in[i]!=(0xb000|seq))good=0;
    }
    if(ready)break;
   }
   /* The final failed attempt is acknowledged before either peer reports it. */
   unsigned decision=(good?0xf800:0xf000)|seq;
   if(!host_echo((uint16_t)decision))return 0;
   if(good)return wire_finished(1);
  }else{
   for(unsigned i=0;i<total;i++)packed[i]=0;
   unsigned received=0,checked=0,good=0;uint16_t response=wire_last_sent;
   for(;;){
    uint16_t in[4];if(!receive_completed(in,LE_RECEIVE_TIMEOUT,seq))return 0;unsigned word=in[0],tag=word&0xf000;
    if(!checked&&(tag==0x9000||tag==0xa000)){
     unsigned expected=received&1?0xa000:0x9000;
     if(tag==expected&&received<words){block_put(packed,total,received++,word);response=(uint16_t)word;}
     else if(received&&tag==((received-1)&1?0xa000:0x9000)){
      /* A corrected repeat replaces the last word, rather than appending it. */
      block_put(packed,total,received-1,word);response=(uint16_t)word;
     }
    }else if(word==stop){
     if(!checked){
      good=received==words;
      if(good&&codec)good=coop_lz_unpack(packed+4,size,decoded,bytes);
      else if(good)for(unsigned i=0;i<bytes;i++)decoded[i]=packed[4+i];
      uint32_t crc=0;for(unsigned i=0;i<4;i++)crc|=(uint32_t)packed[i]<<(8*i);
      good=good&&coop_block_crc(decoded,bytes,seq)==crc;checked=1;diag_phase(LP_BLOCK_CRC);
     }
     response=(uint16_t)((good?0xb000:0xc000)|seq);
    }else if(checked&&(word==(0xf800|seq)||word==(0xf000|seq))){
     if(word==(0xf800|seq)&&!good)return link_diag_fault(LE_BLOCK_CRC,1,0);
     if(word==(0xf800|seq))for(unsigned i=0;i<bytes;i++)data[i]=decoded[i];
     wire_send((uint16_t)word);
     if(word==(0xf800|seq))return wire_finished(1);
     break;
    }
    wire_send(response);
   }
  }
 }
 return link_diag_fault(LE_BLOCK_CRC,1,0);
}
