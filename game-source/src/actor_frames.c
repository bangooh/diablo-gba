#include "actor_frames.h"
#include "lz.h"
#define ROM_FN __attribute__((section(".text.ui")))
#define CLIP_MAGIC 0x31525053u
#define RAW_TILE_MAGIC 0x32525053u
/* SPR2: magic, frame count, total bytes, aligned record offsets. A record is
 * packed tile bytes, two presence masks, then the nonempty 32-byte tiles.
 * ROM assets own the source allocation, as with SPR1/LZ; offsets/lengths are
 * checked against the declared clip length before any destination is changed.
 * Independent records need no scratch or persistent decoder state. */
static ROM_FN const uint32_t *raw_tile_record(const uint8_t *clip,unsigned index){
 if((uintptr_t)clip&3)return 0;
 unsigned count=lz_word(clip+4),limit=lz_word(clip+8);
 if(limit<12||limit>32u*1024*1024||!count||count>(limit-12)/4||index>=count)return 0;
 unsigned first=12+count*4,offset=lz_word(clip+12+index*4);
 if((offset&3)||offset<first||offset>limit-12)return 0;
 const uint32_t *record=(const uint32_t*)(clip+offset);
 unsigned bytes=record[0],low=record[1],high=record[2],tiles=0;
 if(bytes>2048||(bytes&31)||bytes>limit-offset-12)return 0;
 for(unsigned m=low;m;m&=m-1)tiles++;
 for(unsigned m=high;m;m&=m-1)tiles++;
 if(bytes!=tiles*32)return 0;
 return record;
}
ROM_FN unsigned actor_frame_tiles(const uint8_t *clip,unsigned index,uint32_t tiles[2]){
 if(lz_word(clip)!=RAW_TILE_MAGIC)return 0;
 const uint32_t *record=raw_tile_record(clip,index);if(!record)return 0;
 tiles[0]=record[1];tiles[1]=record[2];return 1;
}
static ROM_FN unsigned raw_tile_frame(const uint8_t *clip,unsigned index,void *destination,unsigned capacity){
 if(capacity<2048||((uintptr_t)destination&3))return 0;
 const uint32_t *record=raw_tile_record(clip,index);if(!record)return 0;
 unsigned low=record[1],high=record[2];
 uint32_t *dst=destination;const uint32_t *src=record+3;
 for(unsigned half=0;half<2;half++){
  unsigned bits=half?high:low;
  for(unsigned tile=0;tile<32;tile++,bits>>=1,dst+=8){
   if(bits&1){
    dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=src[3];
    dst[4]=src[4];dst[5]=src[5];dst[6]=src[6];dst[7]=src[7];src+=8;
   }else{dst[0]=dst[1]=dst[2]=dst[3]=dst[4]=dst[5]=dst[6]=dst[7]=0;}
  }
 }
 return 2048;
}
static inline __attribute__((always_inline)) int nibble(const uint8_t *data,unsigned length,unsigned *cursor){
 if(*cursor>=length*2)return -1;
 unsigned n=*cursor;(*cursor)++;return (data[n>>1]>>((n&1)*4))&15;
}
/* Sparse actor patches are a hot animation path. Keep this small unchanged
 * decoder in IWRAM; its callers and asset streams remain in ROM. */
static __attribute__((noinline)) unsigned xor_nibbles(const uint8_t *data,unsigned length,uint8_t *dst){
 unsigned cursor=0,pos=0;
 while(pos<4096){
  int value=nibble(data,length,&cursor);if(value<0)return 0;
  if(value){dst[pos>>1]^=(uint8_t)(value<<((pos&1)*4));pos++;continue;}
  int run=nibble(data,length,&cursor);if(run<0)return 0;
  if(!run||run==15){
   int a=nibble(data,length,&cursor),b=nibble(data,length,&cursor);if(a<0||b<0)return 0;
   if(!run){int c=nibble(data,length,&cursor);if(c<0)return 0;run=271+a+(b<<4)+(c<<8);}
   else run=15+a+(b<<4);
  }
  if((unsigned)run>4096-pos)return 0;pos+=(unsigned)run;
 }
 if(cursor<length*2){if(cursor+1!=length*2||nibble(data,length,&cursor))return 0;}
 return 2048;
}
/* Up to three sparse XOR patches on one independent key frame. No recursion,
 * allocation, persistent decompression state, or actor-dependent cache keys. */
ROM_FN unsigned actor_frame_external(const uint8_t *clip,unsigned index,void *destination,unsigned capacity,
                            void *scratch,const uint8_t *pool,const uint8_t *external_pool){
 unsigned magic=lz_word(clip);
 if(magic==RAW_TILE_MAGIC)return raw_tile_frame(clip,index,destination,capacity);
 if(magic!=CLIP_MAGIC&&magic!=0x33525053u)return lz_block(clip,index,destination,capacity);
 if(capacity<2048||index>=lz_word(clip+4)||!pool||!scratch||scratch==destination)return 0;
 if(((uintptr_t)destination|(uintptr_t)scratch)&3)return 0;
 unsigned limit=lz_word(pool),offset=lz_word(clip+8+index*4),chain[4],count=0;
 if(limit<8||lz_word(pool+4)!=0x31465053u)return 0;
 for(;;){
  if(count==4||offset<8||offset>limit-4)return 0;
  const uint8_t *p=pool+offset;unsigned bytes=p[2]|(p[3]<<8);
  if(bytes>limit-offset-4||p[0]>5||p[1]>3)return 0;
  chain[count++]=offset;
  if(p[0]==5){
   if(p[1]||bytes<4||!external_pool||external_pool==pool)return 0;
   uint32_t ref[3]={CLIP_MAGIC,1,lz_word(p+4)};
   if(actor_frame((const uint8_t*)ref,0,destination,capacity,scratch,external_pool)!=2048||xor_nibbles(p+8,bytes-4,destination)!=2048)return 0;
   break;
  }
  if(!p[0]||p[0]==4){
   if(p[1])return 0;
   if(!p[0]){if(bytes<4||lz_decode(p+4,destination,capacity)!=2048)return 0;}
   else{for(unsigned i=0;i<512;i++)((uint32_t*)destination)[i]=0;if(xor_nibbles(p+4,bytes,destination)!=2048)return 0;}
   break;
  }
  if(bytes<4||!p[1])return 0;
  unsigned next=lz_word(p+4);
  if(next>=offset||next<8||next>limit-4||pool[next+1]+1!=p[1])return 0;
  offset=next;
 }
 uint8_t *dst=destination;
 while(--count){
  const uint8_t *p=pool+chain[count-1];unsigned length=(p[2]|(p[3]<<8))-4;
  const uint8_t *data=p+8;
  if(p[0]==3){if(xor_nibbles(data,length,dst)!=2048)return 0;continue;}
  if(p[0]==2){length=lz_decode(data,scratch,2048);if(!length)return 0;data=scratch;}
  unsigned pos=0,n=0;
  while(n<length){
   unsigned token=data[n++],run=(token&127)+1;
   if(run>2048-pos)return 0;
   if(token<128){if(run>length-n)return 0;for(unsigned j=0;j<run;j++)dst[pos+j]^=data[n++];}
   pos+=run;
  }
  if(pos!=2048)return 0;
 }
 return 2048;
}

ROM_FN unsigned actor_frame(const uint8_t *clip,unsigned index,void *destination,unsigned capacity,void *scratch,const uint8_t *pool){return actor_frame_external(clip,index,destination,capacity,scratch,pool,0);}
