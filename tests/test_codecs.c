#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lz.h"
#include "coop_codec.h"
int main(void){
 uint8_t src[256],packed[300],dst[258];unsigned seed=1;
 for(unsigned mode=0;mode<3;mode++)for(unsigned n=8;n<=256;n++){
  for(unsigned i=0;i<n;i++){seed=seed*1664525u+1013904223u;src[i]=mode==0?42:mode==1?i%7:seed>>24;}
  unsigned bytes=coop_lz_pack(src,n,packed,sizeof(packed));assert(bytes);
  memset(dst,0xa5,sizeof(dst));
  assert(coop_lz_unpack(packed,bytes,dst+1,n));assert(!memcmp(src,dst+1,n));
  assert(dst[0]==0xa5&&dst[n+1]==0xa5);
  assert(lz_decode(packed,dst+1,n)==n&&!memcmp(src,dst+1,n));
  assert(dst[0]==0xa5&&dst[n+1]==0xa5);
  assert(!lz_decode(packed,dst+1,n-1));
  for(unsigned cut=0;cut<bytes;cut++)assert(!coop_lz_unpack(packed,cut,dst+1,n));
  packed[bytes]=0;assert(!coop_lz_unpack(packed,bytes+1,dst+1,n));
 }
 uint8_t invalid_distance[]={16,3,0,0,128,0,0};
 uint8_t invalid_length[]={16,2,0,0,64,42,0,0};
 assert(!lz_decode(invalid_distance,dst,256));assert(!lz_decode(invalid_length,dst,256));
 assert(!coop_lz_unpack(invalid_distance,sizeof(invalid_distance),dst,3));
 assert(!coop_lz_unpack(invalid_length,sizeof(invalid_length),dst,2));
 assert(coop_block_crc(src,256,1)!=coop_block_crc(src,256,2));
 puts("PASS: LZ/coop round trips, truncated packets, invalid references and output guards");
}
