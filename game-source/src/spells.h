#pragma once
enum { SPELL_FIREBOLT=1,SPELL_HEAL=2,SPELL_COMMAND=1536 };
enum { SPELL_LIGHTNING=3,SPELL_FLASH=4,SPELL_FIREWALL=6,SPELL_TOWN=7,
 SPELL_STONE=8,SPELL_INFRA=9,SPELL_PHASING=10,SPELL_SHIELD=11,SPELL_FIREBALL=12,
 SPELL_GUARDIAN=13,SPELL_CHAIN=14,SPELL_WAVE=15,SPELL_NOVA=18,
 SPELL_INFERNO=20,SPELL_GOLEM=21,SPELL_TELEPORT=23,SPELL_APOCALYPSE=24,
 SPELL_ELEMENTAL=29,SPELL_CHARGED=30,SPELL_HOLY=31,SPELL_RESURRECT=32,
 SPELL_TELEKINESIS=33,SPELL_HEAL_OTHER=34,SPELL_BLOOD_STAR=35,SPELL_BONE_SPIRIT=36,
 SPELL_MAX=36, SPELL_STAFF=37, SPELL_SKILL=38, SPELL_CHOICES=39 };
/* SPELL_STAFF is a quick-slot source, never a native spell/effect ID. */
typedef struct {const char *name;uint8_t mana,adjust,minimum,magic,book,element,town,staff_level,staff_min,staff_max;} SpellDef;
extern const SpellDef spell_defs[SPELL_MAX+1];
unsigned game_spell_level(const HeroProgress *p,unsigned spell); /* learned level only */
unsigned game_spell_effective_level(unsigned spell);
int game_spell_supported(unsigned spell);
int game_spell_available(unsigned spell);
/* Original source kinds; HUD color only, never serialized or a cast command. */
enum { SPELL_ICON_SKILL=0,SPELL_ICON_LEARNED=1,SPELL_ICON_SCROLL=2,SPELL_ICON_STAFF=3,SPELL_ICON_INVALID=4 };
unsigned game_spell_icon_key(const Game *g,unsigned choice); /* spell | source kind << 8 */
int game_spell_choice_valid(unsigned choice);
unsigned game_spell_id(unsigned choice);
int game_spell_portal_at(const Game *g,unsigned owner,int *x,int *y);
void game_spell_portal_consume(unsigned owner);
int game_spell_portal_use(Game *g);
int game_spell_portal_target(const Game *g);
int game_spell_portal_destination(unsigned owner);
unsigned game_spell_effect_pose(unsigned index,unsigned clock,unsigned *frame);
void game_spell_new_campaign(void);
int game_spell_shielded(unsigned player);
int game_spell_shrine_effect(Game *g,unsigned spell,unsigned level);
const char *game_spell_name(unsigned spell);
unsigned game_spell_book(uint32_t seed,unsigned depth);
unsigned game_spell_staff(uint32_t seed,unsigned level,int multiplayer);
unsigned game_spell_wire(unsigned operation,unsigned slot,unsigned spell);
#define SPELL_SKILL_SWING (-67-SPELL_SKILL)
#define SPELL_CASTING(s) (((s)<=-68 && (s)>=-103) || (s)==SPELL_SKILL_SWING)
#define SPELL_FROM_SWING(s) ((unsigned)(-67-(s)))
void game_spell_projectiles_step(Game *g);
unsigned game_spell_missile(unsigned index);
/* Fixed-capacity shared cast headers and compact independently moving lanes. */
enum { MAX_SPELL_BOLTS=16, MAX_SPELL_CASTS=8 };
typedef struct {uint16_t ticks,power;uint8_t spell,level,owner,count;} SpellCast;
typedef struct {int16_t x,y,vx,vy;int8_t last_x,last_y;uint8_t cast;int8_t target;} SpellBolt;
extern SpellCast game_spell_casts[MAX_SPELL_CASTS];
extern SpellBolt game_spell_bolts[MAX_SPELL_BOLTS];
static inline int game_spell_bolts_active(void){for(unsigned i=0;i<MAX_SPELL_CASTS;i++)if(game_spell_casts[i].count)return 1;return 0;}
unsigned game_spell_bolt_world(unsigned index,unsigned phase,int *wx,int *wy);

enum { SPELL_EFFECT_VIEWS=16+4*16+4 };
unsigned game_spell_effects_active(void);
unsigned game_spell_firewall_stage(unsigned index);
unsigned game_spell_effect_view_phase(unsigned index,unsigned phase,int *wx,int *wy);
unsigned game_spell_effect_view(unsigned index,int *world_x,int *world_y);
int game_spell_firewall_at(int x,int y);
int game_spell_stoned(unsigned monster);
unsigned game_spell_absorb(Game *g,unsigned damage);
unsigned game_spell_absorb_q6(Game *g,unsigned damage);
int game_spell_golem_hit(Game *g,unsigned monster,unsigned damage);
/* The wire transports 12 payload bits, not 16. Item-command bit10 plus
 * unused bit9 identifies spells. Eight scattered bits encode operations;
 * bits2..3 remain host clock. See game_spell_wire. */
extern uint8_t spell_pending[4];
extern volatile unsigned spell_casts,spell_failures,spell_audio_event;
unsigned game_spell_cost(unsigned spell);
unsigned game_spell_frames(unsigned cls);
unsigned game_spell_release(unsigned cls);
int game_spell_request(Game *g,unsigned spell);
int game_spell_control(Game *g);
void game_spell_grant(unsigned spell);
int game_spell_learn(Game *g,unsigned spell);
unsigned game_spell_requirement(unsigned spell);
void game_spells_reset(void);
int game_spell_request_scroll(Game *g,unsigned index);
void game_spell_cancel(Game *g);
void game_spell_revive(Game *g);

uint32_t game_spells_hash(uint32_t h);

/* Slots: UP, RIGHT, DOWN, LEFT. Selection is separate from casting. */
void game_spell_select_slot(unsigned slot);
int game_spell_bind_slot(unsigned slot,unsigned spell);
int game_spell_request_selected(Game *g);
/* Presentation only: recipient of an accepted/active support cast, never
 * merely the selected quick slot. Uses the same eligibility as the effect. */
int game_spell_player_target(const Game *g);
void game_spell_command(Game *g,unsigned command);
