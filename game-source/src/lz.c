#include "lz.h"
/* Sparse 64x64 4bpp frames expand backwards inside the existing actor cache.
 * Keep this cold path in ROM; the original LZ loop stays in IWRAM. */
static __attribute__((section(".text.ui"),noinline)) unsigned compact_decode(const uint8_t *src,void *destination,unsigned capacity){
 if(capacity<2048)return 0;
 unsigned tiles=0;for(unsigned i=0;i<8;i++)for(unsigned m=src[1+i];m;m>>=1)tiles+=m&1;
 unsigned size=tiles*32;
 if(src[9]!=16||(lz_word(src+9)>>8)!=size||lz_decode(src+9,destination,size)!=size)return 0;
 uint8_t *dst=destination;
 if(!((uintptr_t)dst&3)){
  uint32_t *words=destination;size>>=2;
  for(int tile=63;tile>=0;tile--){
   if(src[1+((unsigned)tile>>3)]&(1u<<(tile&7))){
    size-=8;for(int j=7;j>=0;j--)words[tile*8+j]=words[size+(unsigned)j];
   }else for(unsigned j=0;j<8;j++)words[tile*8+j]=0;
  }
  return 2048;
 }
 for(int tile=63;tile>=0;tile--){
  if(src[1+((unsigned)tile>>3)]&(1u<<(tile&7))){
   size-=32;for(int j=31;j>=0;j--)dst[tile*32+j]=dst[size+(unsigned)j];
  }else for(unsigned j=0;j<32;j++)dst[tile*32+j]=0;
 }
 return 2048;
}
unsigned lz_word(const uint8_t *p){return p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned)p[3]<<24);}
unsigned lz_decode(const uint8_t *src,void *destination,unsigned capacity){
 if(src[0]==17)return compact_decode(src,destination,capacity);
 if(src[0]!=16)return 0;
 unsigned size=lz_word(src)>>8,pos=0;src+=4;uint8_t *dst=destination;
 if(size>capacity)return 0;
 while(pos<size){unsigned flags=*src++;
  for(unsigned bit=128;bit && pos<size;bit>>=1){
   if(flags&bit){unsigned a=*src++,b=*src++,n=(a>>4)+3,d=((a&15)<<8)+b+1;
    if(d>pos || n>size-pos)return 0;
    while(n && ((uintptr_t)(dst+pos)&3)){dst[pos]=dst[pos-d];pos++;n--;}
    if(d==1){
     unsigned value=(unsigned)dst[pos-1]*0x01010101u;
     while(n>=4){*(uint32_t*)(dst+pos)=value;pos+=4;n-=4;}
    }else if(d>=4 && !(d&3)){
     while(n>=4){*(uint32_t*)(dst+pos)=*(const uint32_t*)(dst+pos-d);pos+=4;n-=4;}
    }
    while(n--){dst[pos]=dst[pos-d];pos++;}
   }else dst[pos++]=*src++;
  }
 }
 return size;
}
unsigned lz_block(const uint8_t *src,unsigned index,void *dst,unsigned capacity){
 if(index>=lz_word(src))return 0;
 return lz_decode(src+lz_word(src+4+index*4),dst,capacity);
}
