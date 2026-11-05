/* EWRAM is nearly full; keep diagnostic state in IWRAM, leaving the stack intact.
 * Capture registers BEFORE coop_wire_end clears them. The first cause wins. */
#include "link_diag.h"
#include "flash.h"
volatile LinkDiagnostic link_diag __attribute__((section(".iwram.diag"),aligned(4)))={0};
void link_diag_start(unsigned id,unsigned members){
 volatile uint32_t *p=(volatile uint32_t*)&link_diag;
 for(unsigned i=0;i<sizeof(link_diag)/4;i++)p[i]=0;
 link_diag.enabled=1;link_diag.screen=0; /* Release: retain counters without boot debug overlay. */link_diag.id=id;link_diag.members=members;
 link_diag.lobby_errors=link_state.errors;link_diag.lobby_packets=link_state.packets;
 wire_seq=elapsed=deadline=0;
}
void link_diag_stage(unsigned stage){
 if(!link_diag.enabled||link_diag.error)return;
 link_diag.stage=stage;link_diag.progress=link_diag.total=0;
 if(link_diag.screen)link_diag_draw_stage();
}
void link_diag_position(unsigned progress,unsigned total){
 if(!link_diag.enabled||link_diag.error)return;
 link_diag.progress=progress;link_diag.total=total;
}
int link_diag_fault(unsigned error,unsigned expected,unsigned actual){
 if(link_diag.enabled&&!link_diag.error){
  link_diag.error=error;link_diag.expected=expected;link_diag.actual=actual;
  link_diag.sequence=wire_seq;link_diag.elapsed=elapsed;link_diag.limit=deadline;
  link_diag.sio=REG(0x04000128);link_diag.irq=REG(0x04000202);link_diag.rcnt=REG(0x04000134);
  link_diag.tx=REG(0x0400012a);
  for(unsigned i=0;i<4;i++)link_diag.rx[i]=REG(0x04000120+i*2);
  link_diag.flash_status=flash_status;link_diag.flash_device=flash_device;link_diag.flash_read=flash_last_read;
 }
 return 0;
}
static void diag_phase(unsigned phase){if(!link_diag.error)link_diag.phase=phase;}
static void diag_transfer(void){if(!link_diag.error)link_diag.transfers++;}
