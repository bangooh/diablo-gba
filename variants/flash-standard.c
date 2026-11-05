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
#ifdef FLASH_EMULATOR_TEST
extern uint8_t flash_bus_read(unsigned);
extern void flash_bus_write(unsigned,uint8_t);
#define READ(n) flash_bus_read(n)
#define WRITE(n,v) flash_bus_write(n,v)
#else
#define FLASH ((volatile uint8_t*)0x0e000000)
#define READ(n) FLASH[n]
#define WRITE(n,v) (FLASH[n]=(v))
#endif
__attribute__((used,section(".save_signature"),aligned(4))) const char flash_signature[]="FLASH1M_V103";
static void command(uint8_t c){WRITE(0x5555,0xaa);WRITE(0x2aaa,0x55);WRITE(0x5555,c);}
static void select_bank(unsigned n){command(0xb0);WRITE(0,(uint8_t)n);}
static uint8_t read_byte(unsigned n){return READ(n);}
/* Bounded waits prevent a missing/unsupported save device hanging the game. */
static int wait_byte(unsigned n,uint8_t v){for(unsigned i=0;i<4000000;i++)if(READ(n)==v)return 1;command(0xf0);return 0;}
static int erase_sector(unsigned n){command(0x80);WRITE(0x5555,0xaa);WRITE(0x2aaa,0x55);WRITE(n,0x30);return wait_byte(n,255);}
/* Delta VBA-M erases backing bytes to zero. Skip FF only when already FF;
 * otherwise issue an ordinary program command and verify, as for other bytes. */
static int program(unsigned n,uint8_t v){if(v==255&&read_byte(n)==255)return 1;command(0xa0);WRITE(n,v);return wait_byte(n,v);}
static int supported(void){
 command(0x90);unsigned manufacturer=READ(0),device=READ(1);flash_device=manufacturer|(device<<8);command(0xf0);
 return (manufacturer==0xc2&&device==0x09)||(manufacturer==0x62&&device==0x13);
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
