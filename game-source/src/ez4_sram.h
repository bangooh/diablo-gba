/* EZ-IV experimental backend: 64 KiB windows at SRAM pages 0x10/0x20.
 * Register protocol and save mapping: ez-flash/ez3pda inram.cpp and
 * savermanage.cpp (ReadFileToSram). Not a generic flashcart backend.
 * This changes SRAM mapping only; never enables NOR/firmware writes. */
#ifndef EZ4_SRAM_H
#define EZ4_SRAM_H
#include <stdint.h>
#ifdef EZ4_HOST_TEST
extern void ez4_host_page(unsigned);
extern uint8_t ez4_host_read(unsigned);
extern void ez4_host_write(unsigned,uint8_t);
static void ez4_bank(unsigned bank){ez4_host_page(0x10+(bank<<4));}
static uint8_t ez4_read(unsigned offset){return ez4_host_read(offset);}
static void ez4_write(unsigned offset,uint8_t value){ez4_host_write(offset,value);}
#else
#define EZ4_RAM_FN __attribute__((noinline,section(".iwram.diag")))
static EZ4_RAM_FN void ez4_bank(unsigned bank){
 volatile uint16_t *ime=(volatile uint16_t*)0x04000208;
 uint16_t saved=*ime;*ime=0;
 *(volatile uint16_t*)0x09fe0000=0xd200;
 *(volatile uint16_t*)0x08000000=0x1500;
 *(volatile uint16_t*)0x08020000=0xd200;
 *(volatile uint16_t*)0x08040000=0x1500;
 *(volatile uint16_t*)0x09c00000=(uint16_t)(0x10+(bank<<4));
 *(volatile uint16_t*)0x09fc0000=0x1500;
 *ime=saved;
}
static EZ4_RAM_FN uint8_t ez4_read(unsigned offset){
 return ((volatile uint8_t*)0x0e000000)[offset];
}
static EZ4_RAM_FN void ez4_write(unsigned offset,uint8_t value){
 ((volatile uint8_t*)0x0e000000)[offset]=value;
}
#endif
#endif
