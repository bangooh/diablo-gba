#include "flash.h"
volatile unsigned flash_status,flash_device,flash_last_read;
#define MAGIC 0x36414744u
#ifdef FLASH_HOST_TEST
uint8_t flash_test_bytes[FLASH_BYTES];int flash_test_budget=-1;
static unsigned selected;
static void select_bank(unsigned n){selected=n;}
static uint8_t read_byte(unsigned n){return flash_test_bytes[selected*65536+n];}
static int allow(void){if(flash_test_budget<0)return 1;if(!flash_test_budget)return 0;--flash_test_budget;return 1;}
static int erase_sector(unsigned n){if(!allow())return 0;for(unsigned i=0;i<4096;i++)flash_test_bytes[selected*65536+n+i]=255;return 1;}
static int program(unsigned n,uint8_t v){if(!allow())return 0;flash_test_bytes[selected*65536+n]&=v;return read_byte(n)==v;}
static int supported(void){return 1;}
#else
#include "ez4_sram.h"
/* Keep the 128 KiB allocation marker for the EZ-IV loader. The routines
 * below already access SRAM; no SDK save-function patch is required. */
__attribute__((used,section(".save_signature"),aligned(4))) const char flash_signature[]="FLASH1M_V103";
static void select_bank(unsigned n){ez4_bank(n);}
static uint8_t read_byte(unsigned n){return ez4_read(n);}
static int erase_sector(unsigned n){
 for(unsigned i=0;i<4096;i++)ez4_write(n+i,255);
 for(unsigned i=0;i<4096;i++)if(ez4_read(n+i)!=255)return 0;
 return 1;
}
static int program(unsigned n,uint8_t v){ez4_write(n,v);return ez4_read(n)==v;}
/* This is a cartridge-specific build, not automatic hardware detection.
 * 0x455A is a backend tag, never a claimed physical Flash manufacturer ID. */
static int supported(void){
 /* Header byte 31 is unused and outside the committed checksum. Probe only
  * this byte, restore both values, and refuse to erase if banks alias. */
 select_bank(0);uint8_t a=read_byte(31);
 select_bank(1);uint8_t b=read_byte(31);
 select_bank(0);ez4_write(31,0x5a);
 select_bank(1);ez4_write(31,0xa5);
 select_bank(0);int ok=read_byte(31)==0x5a;
 select_bank(1);ok=ok&&read_byte(31)==0xa5;
 select_bank(0);ez4_write(31,a);
 select_bank(1);ez4_write(31,b);select_bank(0);
 flash_device=0x455A;return ok;
}
#endif
static uint32_t word(unsigned o){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)read_byte(o+i)<<(8*i);return v;}
static int put_word(unsigned o,uint32_t v){for(unsigned i=0;i<4;i++)if(!program(o+i,(uint8_t)(v>>(8*i))))return 0;return 1;}
static uint32_t checksum(unsigned n){uint32_t h=2166136261u;for(unsigned i=4;i<16;i++)h=(h^read_byte(i))*16777619u;for(unsigned i=0;i<n;i++)h=(h^read_byte(32+i))*16777619u;return h;}
static int valid(unsigned n,unsigned version){return word(0)==MAGIC&&word(4)==version&&word(8)==n&&word(16)==checksum(n);}
static int newest(unsigned n,unsigned v){
 select_bank(0);int a=valid(n,v);uint32_t ga=word(12);select_bank(1);int b=valid(n,v);uint32_t gb=word(12);
 if(!a)return b?1:-1;
 if(!b)return 0;
 return gb-ga&&gb-ga<0x80000000u?1:0;
}
int flash_save_empty(void){
 for(unsigned bank=0;bank<2;bank++){
  select_bank(bank);
  for(unsigned i=0;i<FLASH_BANK_BYTES;i++)if(read_byte(i)!=255){select_bank(0);return 0;}
 }
 select_bank(0);return 1;
}
int flash_save_read_range(void *out,unsigned n,unsigned v,unsigned offset,unsigned count){
 if(n>65504||offset>n||count>n-offset)return 0;
 int bank=newest(n,v);flash_last_read=(unsigned)(bank+1);
 if(bank<0){select_bank(0);return 0;}
 select_bank((unsigned)bank);for(unsigned i=0;i<count;i++)((uint8_t*)out)[i]=read_byte(32+offset+i);
 select_bank(0);return 1;
}
int flash_save_patch(unsigned n,unsigned v,const FlashPatch *patches,unsigned count,int reset){
 flash_status=1;
 if(n>65504||!supported())goto fail_patch;
 for(unsigned p=0;p<count;p++)if(patches[p].offset>n||patches[p].bytes>n-patches[p].offset)goto fail_patch;
 int source=newest(n,v),old=source;
 /* Incremental commits must never invent the missing unchanged floors. */
 if(source<0&&!reset)goto fail_patch;
 uint32_t generation=1;if(old>=0){select_bank((unsigned)old);generation=word(12)+1;}
 unsigned target=old<0?0:(unsigned)(old^1);select_bank(target);flash_status=2;
 for(unsigned o=0;o<n+32;o+=4096)if(!erase_sector(o))goto fail_patch;
 if(!put_word(4,v)||!put_word(8,n)||!put_word(12,generation))goto fail_patch;
 uint8_t chunk[256];uint32_t hash=2166136261u;
 for(unsigned i=4;i<16;i++)hash=(hash^read_byte(i))*16777619u;
 for(unsigned base=0;base<n;base+=sizeof(chunk)){
  unsigned length=n-base;if(length>sizeof(chunk))length=sizeof(chunk);
  if(source>=0&&!reset){select_bank((unsigned)source);for(unsigned i=0;i<length;i++)chunk[i]=read_byte(32+base+i);}
  else for(unsigned i=0;i<length;i++)chunk[i]=0;
  for(unsigned p=0;p<count;p++)for(unsigned i=0;i<length;i++)if(base+i>=patches[p].offset&&base+i-patches[p].offset<patches[p].bytes)chunk[i]=((const uint8_t*)patches[p].data)[base+i-patches[p].offset];
  select_bank(target);
  for(unsigned i=0;i<length;i++){flash_status=10000+base+i;hash=(hash^chunk[i])*16777619u;if(!program(32+base+i,chunk[i]))goto fail_patch;}
 }
 /* The expected checksum comes from RAM/source, not the newly programmed bank. */
 if(checksum(n)!=hash||!put_word(16,hash)||!put_word(0,MAGIC)||!valid(n,v))goto fail_patch;
 flash_status=4;select_bank(0);return 1;
fail_patch:select_bank(0);return 0;
}
