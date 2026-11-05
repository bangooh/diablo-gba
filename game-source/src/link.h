#ifndef LINK_H
#define LINK_H
#include <stdint.h>
/* v110 lobby protocol. A starts the separate co-op session/character import. */
/* Stage38 changes melee target decisions; reject mixed-version lockstep. */
enum { LINK_WORDS=10, LINK_MAGIC=0xd094, LINK_VERSION=0xd108, LINK_SYNC=0xf094, LINK_INPUT=1, LINK_STATE=2, LINK_TIMEOUT=120 };
typedef struct {
 uint16_t window[4][LINK_WORDS],tx[LINK_WORDS];
 uint16_t sequence,received[4],keys,remote_keys[4],scene,positions[4],facing[4];
 unsigned packets,errors,age[4],role,connected,have_sequence,tx_index,active;
 unsigned members,launch,launch_acks,game_ready,cpu_mask;
 unsigned last_start;
} LinkState;
void link_reset(LinkState *s,unsigned role);
void link_receive(LinkState *s,unsigned peer,uint16_t word);
void link_frame(LinkState *s,unsigned keys);
uint16_t link_next_word(LinkState *s);
uint16_t link_crc(const uint16_t *p);
extern LinkState link_state;
void link_open(void);
void link_close(void);
void link_poll(void);
void link_refresh(unsigned keys);
#endif
