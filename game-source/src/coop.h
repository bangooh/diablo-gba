#ifndef COOP_H
#define COOP_H
#include "game.h"
typedef struct {
 CharacterState hero;
 int16_t x,y,kills;
 uint16_t move_wait,attack_wait,flash,hit_flash,previous;
 uint8_t facing,attack_queued,notice,notice_ticks;
 int8_t target,swing;
 uint8_t damage_min,damage_max,armor,weapon_flags,attack_duration,reaction_duration,reaction_block,town_message;
} CoopPlayer;
extern CoopPlayer coop_players[4];
extern unsigned coop_cpu_mask;
void coop_cpu_reset(void);
void coop_cpu_step(Game *g,unsigned id);
void coop_cpu_equip(CharacterState *h,uint32_t seed);
unsigned coop_cpu_input(Game *g,unsigned id);
int coop_restart_entrance(Game *g);
extern unsigned coop_active,coop_id,coop_members,coop_turn,coop_ticks;
extern unsigned coop_campaign_active;
extern uint32_t coop_campaign_epoch;
/* Shared co-op intentionally retains the host's single-player quest layouts. */
static inline int game_campaign_layout(void){return !coop_active||coop_campaign_active;}
typedef struct {uint8_t pending,destination,actor;int8_t owner;int16_t x,y;} CoopRegionRequest;
extern CoopRegionRequest coop_region_request;
int coop_request_region(unsigned destination,int x,int y,int owner);
int coop_region_pending(void);
void coop_region_clear(void);
/* Called after an acknowledged actor step, while that actor is selected. */
void game_coop_actor_interaction(Game *g,unsigned pressed);
extern uint32_t coop_hash;
void coop_capture(Game *g,unsigned id);
void coop_select(Game *g,unsigned id);
int coop_blocks(int x,int y,int except);
/* Only player locomotion may pass through CPU allies. Spawns stay strict. */
int coop_movement_blocks(int x,int y,unsigned mover);
int coop_distance(int x,int y);
void coop_ai_target(Game *g,int i);
void coop_ai_finish(Game *g);
extern uint8_t *coop_enemy_indices;
extern const Game *coop_enemy_game;
unsigned coop_enemy_total(const Game *g);
void coop_enemy_invalidate(void);
static inline unsigned coop_enemy_index(const Game *g,unsigned n){return coop_enemy_indices&&coop_enemy_game==g?coop_enemy_indices[n]:n;}
void coop_sim_step(Game *g,const uint16_t keys[4]);
void coop_checksum(const Game *g);
/* Synchronous, acknowledged input broadcast. No CPU IRQ handler required. */
void coop_set_members(Game *g,unsigned members);
void coop_wire_recovery(unsigned enabled);
unsigned coop_wire_members(void);
int coop_wire_remove(unsigned removed);
void coop_wire_park(void);
int coop_wire_begin(unsigned id,unsigned members);
int coop_wire_input(uint16_t local,uint16_t values[4],uint16_t hash,unsigned pending,unsigned *control);
int coop_wire_frame(uint16_t local,uint16_t values[4],uint16_t hash);
/* Host-only bulk payload, <=256 bytes; block CRC, bounded LZ and retry. */
int coop_wire_block(void *data,unsigned bytes);
/* Host Flash commits may exceed the ordinary3.66-second cable deadline. */
int coop_wire_commit_frame(uint16_t local,uint16_t values[4],uint16_t hash);
void coop_wire_end(void);
/* Called only after every peer has received the final save-result frame. */
int coop_wire_finish(void);
#endif

unsigned coop_wire_missing(void);
