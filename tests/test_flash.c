/* Budget faults model power loss between erase/program operations. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "flash.h"
static unsigned char snapshot[FLASH_BYTES],old[96],next[96],out[96];
static void check(const unsigned char *expected){
 assert(flash_save_read_range(out,sizeof(out),131,0,sizeof(out)));
 assert(!memcmp(out,expected,sizeof(out)));
}
static void cuts(int initial){
 memcpy(snapshot,flash_test_bytes,sizeof(snapshot));
 FlashPatch p={0,sizeof(next),next};
 int succeeded=0;
 for(int budget=0;budget<200;budget++){
  memcpy(flash_test_bytes,snapshot,sizeof(snapshot));flash_test_budget=budget;
  int ok=flash_save_patch(sizeof(next),131,&p,1,initial);
  flash_test_budget=-1;
  if(ok){check(next);succeeded=1;break;}
  if(initial)assert(!flash_save_read_range(out,sizeof(out),131,0,sizeof(out)));
  else check(old);
 }
 assert(succeeded);
}
int main(void){
 for(unsigned i=0;i<sizeof(old);i++){old[i]=(unsigned char)(i*7);next[i]=(unsigned char)(255-i);}
 memset(flash_test_bytes,255,FLASH_BYTES);assert(flash_save_empty());
 assert(!flash_save_patch(sizeof(old),131,0,0,0));cuts(1);
 FlashPatch p={0,sizeof(old),old};
 assert(flash_save_patch(sizeof(old),131,&p,1,0));check(old);
 cuts(0); /* Reuses a previously committed target bank. */
 unsigned bank=flash_last_read-1;flash_test_bytes[bank*FLASH_BANK_BYTES+32]^=1;
 check(old); /* Corrupt newest bank must fall back to the older commit. */
 assert(!flash_save_read_range(out,sizeof(out),132,0,sizeof(out)));
 assert(!flash_save_read_range(out,sizeof(out),131,95,2));
 memcpy(snapshot,flash_test_bytes,FLASH_BYTES);
 p=(FlashPatch){95,2,next};assert(!flash_save_patch(96,131,&p,1,0));
 assert(!memcmp(snapshot,flash_test_bytes,FLASH_BYTES));
 /* Maximum archive size crosses all sectors and both banks. */
 assert(flash_save_patch(65504,200,0,0,1));
 p=(FlashPatch){65503,1,next};assert(flash_save_patch(65504,200,&p,1,0));
 assert(flash_save_read_range(out,65504,200,65503,1)&&out[0]==next[0]);
 puts("PASS: flash initial/update interruption sweep, fallback, bounds and full bank");
}
