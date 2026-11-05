#pragma once
#include <stdint.h>
/* D6 is a diagnostic derivative, not a new production release. */
enum {
 LD_SAVE=1,LD_EMPTY,LD_SYNC,LD_LOCAL_HERO,LD_HERO_BYTES,LD_CPU_SEED,
 LD_HERO_CHECK,LD_HOST_READ,LD_HOST_READY,LD_HEADER,LD_METADATA,LD_FLOOR,
 LD_VALIDATE,LD_GENERATE,LD_GENERATE_READY,LD_STATE_CHECK,LD_COMMIT_READY,
 LD_HOST_WRITE,LD_COMMIT,LD_EPOCH,LD_APPLY,LD_SCENE,LD_RUNNING,
 LD_LEAVE_READY,LD_LEAVE_WRITE,LD_LEAVE_ACK,LD_LEAVE_LINK,LD_LEAVE_DONE
};
enum {
 LE_NONE=0,LE_SAVE_INVALID,LE_TOPOLOGY,LE_READY_TIMEOUT,LE_TRANSFER_TIMEOUT,
 LE_RECEIVE_TIMEOUT,LE_SIO,LE_LOCAL_VALUE,LE_HASH,LE_CONTROL,LE_PEER_REJECT,
 LE_BAD_BYTE,LE_AGREEMENT,LE_HERO_INVALID,LE_HOST_READ,LE_FLOOR_READ,
 LE_METADATA,LE_GENERATION,LE_HOST_WRITE,LE_COMMIT_REJECT,LE_OTHER,
 LE_BLOCK_HEADER,LE_BLOCK_CRC,LE_DEPARTED
};
enum { LP_BEGIN=1,LP_REQUEST,LP_VALUE0,LP_VALUE1,LP_VALUE2,LP_VALUE3,
 LP_HASH,LP_COMMIT,LP_INPUT_VALUES,LP_CLOSE,LP_BLOCK_DATA,LP_BLOCK_CRC,LP_ROSTER };
typedef struct {
 uint32_t enabled,screen,stopped,stage,error,id,members,progress,total;
 uint32_t sequence,phase,elapsed,limit,transfers,expected,actual;
 uint32_t sio,irq,rcnt,tx,rx[4],lobby_errors,lobby_packets;
 uint32_t flash_status,flash_device,flash_read;
 uint32_t busy_polls,snapshot_retries,last_sent;
 uint32_t sio_errors,sio_streak,sio_max_streak,last_bad_sio,last_bad_rx[4];
 uint32_t stop_menu,stop_quick,stop_effects;
 uint32_t wait_active,wait_notices,wait_recovered,wait_ticks,wait_max_ticks;
} LinkDiagnostic;
extern volatile LinkDiagnostic link_diag;
void link_diag_start(unsigned id,unsigned members);
void link_diag_stage(unsigned stage);
void link_diag_position(unsigned progress,unsigned total);
int link_diag_fault(unsigned error,unsigned expected,unsigned actual);
void link_diag_draw_stage(void);
void link_diag_wait(unsigned ticks,unsigned limit);
void link_diag_wait_end(void);
void link_diag_halt(void) __attribute__((noreturn));
