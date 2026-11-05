#ifndef FLASH_H
#define FLASH_H
#include <stdint.h>
/* Two64KiB generations. A commit marker is programmed last. */
enum { FLASH_BYTES=131072, FLASH_BANK_BYTES=65536, FLASH_HEADER_BYTES=32 };
/* Only erased media is absent. Invalid/incompatible nonempty saves are not. */
int flash_save_empty(void);
typedef struct { unsigned offset,bytes; const void *data; } FlashPatch;
int flash_save_read_range(void *out,unsigned bytes,unsigned version,unsigned offset,unsigned count);
/* Copy unchanged ranges from the previous committed bank; reset zero-fills them. */
int flash_save_patch(unsigned bytes,unsigned version,const FlashPatch *patches,unsigned count,int reset);
extern volatile unsigned flash_status,flash_device,flash_last_read;
#ifdef FLASH_HOST_TEST
extern uint8_t flash_test_bytes[FLASH_BYTES];
extern int flash_test_budget;
#endif
#endif
