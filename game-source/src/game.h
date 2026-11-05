#pragma once
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "items.h"
#include "difficulty.h"
#ifdef GRAPHICS_ASSETS
enum { MAP_W=80, MAP_H=80, MACRO_W=40, MAX_ENEMIES=112, MAX_DROPS=128, MAX_PROPS=64, DUNGEON_ORIGIN=1280 };
#else
enum { MAP_W=24, MAP_H=24, MACRO_W=12, MAX_ENEMIES=12, MAX_DROPS=16, MAX_PROPS=64, DUNGEON_ORIGIN=256 };
#endif
enum { KEY_A=1, KEY_B=2, KEY_SELECT=4, KEY_START=8, KEY_RIGHT=16,
       KEY_LEFT=32, KEY_UP=64, KEY_DOWN=128, KEY_R=256, KEY_L=512 };
enum { WALK_TICKS=8, ATTACK_TICKS=18, ATTACK_HIT_WAIT=10 };
enum { ZOMBIE_ATTACK_TICKS=12, ZOMBIE_HIT_WAIT=5 };
/* Tiles are 0..79. Packing coordinates leaves room for individual max HP
 * without increasing the eight-byte actor or any Game/FloorState buffers. */
typedef struct { uint8_t x,y; int16_t hp; uint8_t alive,kind; uint16_t max_hp; } Enemy;
_Static_assert(sizeof(Enemy)==8,"eight-byte monster state");
typedef struct { uint8_t x,y,active,kind; uint32_t seed; uint8_t durability,flags,charges,max_charges; uint16_t enchant,wear_loss; } Drop;
typedef struct { uint8_t x,y,kind,state; } Prop;
/* Native quest IDs reserved for subsequent campaign quests. */
typedef struct { uint16_t visited;uint8_t state[16],stage[16];uint8_t banner_told,bone_gates;Drop reward,reward_anvil,reward_banner,reward_water,reward_mushroom;uint8_t king_told,king_gates;uint16_t finale_flags; } QuestProgress;
extern QuestProgress quest_progress;
/* Original first-contact lines. Only Butcher/Leoric need extra saved flags;
 * the other five already have one-way quest stages. Keep the archive layout. */
enum { BOSS_INTRO_BUTCHER,BOSS_INTRO_KING,BOSS_INTRO_LAZARUS,BOSS_INTRO_WARLORD,
       BOSS_INTRO_GHARBAD,BOSS_INTRO_ZHAR,BOSS_INTRO_SNOTSPIL,
       BOSS_INTRO_COUNT };
enum { BOSS_GEOMETRY_FLAGS=0x1ff,BOSS_SPEECH_FLAGS=0x600,TOWN_WARP_FLAGS=0xe000 };
extern volatile uint32_t boss_audio_event; /* Pending ID bits, not shared state. */
void game_boss_intro(unsigned id);
extern uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
void game_quest_init(QuestProgress *q,uint32_t seed);
int game_quest_valid(const QuestProgress *q);


typedef struct {
    uint8_t map[MAP_H][MAP_W];
    Enemy enemies[MAX_ENEMIES];
    Drop drops[MAX_DROPS];
    int16_t x,y,hp,gold_reserved,kills,potions;
    uint16_t ticks,move_wait,attack_wait,flash,hit_flash;
    uint8_t won,paused;
#ifdef GRAPHICS_ASSETS
    uint8_t facing,attack_queued;
    int8_t target,swing_target;
    uint8_t enemy_attack[MAX_ENEMIES],enemy_facing[MAX_ENEMIES];
    int16_t mana,max_mana;
    uint8_t pickup_notice,pickup_notice_ticks;
    uint8_t area,town_message;
    uint64_t belt; /* Ten 4-bit bottle codes, high24 bits zero. */
    Prop props[MAX_PROPS];
    uint8_t down_x,down_y,attack_duration,reaction_duration,reaction_block;
#endif
    GameGold gold;
#ifdef GRAPHICS_ASSETS
    uint64_t trap_spent;
#endif
} Game;
#ifdef GRAPHICS_ASSETS
/* Persistent character growth is independent of the two world snapshots. */
typedef struct {
 uint32_t experience;
 uint16_t max_hp_q6,max_mana_q6,points;
 uint8_t level,strength,magic,dexterity,vitality,spell_active_slot;
 uint8_t spell_level[2],spell_slot[4],spell_selected,mana_fraction;
 uint8_t spell_extra[17]; /* Original IDs3..36, two 4-bit levels per byte. */
 uint8_t life_drain_fraction; uint16_t mana_debt;
 uint16_t infra_ticks,skill_nonce;uint8_t life_steal_fraction; /* Resource fractions are preserved per hero. */
 uint8_t completion; /* Retail pDiabloKillLevel; uses existing trailing padding. */
} HeroProgress;
_Static_assert(sizeof(HeroProgress)==52,"completion preserves hero progress layout");
/* Rebuilt from base growth + identified, usable equipment; never serialized. */
typedef struct {
 int16_t stats[4],damage_percent,damage_flat,to_hit,damage_taken,spell_bonus;
 uint16_t max_hp,max_mana;int16_t armor;
 uint16_t melee_element[4],arrow_element[4]; /* fire min/max, lightning min/max */
 uint8_t resist[3],usable,weapon_low,weapon_high,weapon_flags,combat_flags,life_steal,mana_steal,light_radius,special_flags,armor_pierce;
} ItemEffects;
enum { ITEM_THORNS=1,ITEM_NOMANA=2,ITEM_ZERORES=4,ITEM_DRAIN=8,ITEM_DEMON=16,ITEM_RANDOM_ARROW=32,ITEM_HALF_TRAP=64,ITEM_INFRA=128 };
extern ItemEffects item_effects;
unsigned item_hero_appearance(const Inventory *bag,const HeroProgress *stats,unsigned cls);
void game_item_effects_step(Game *g);
void item_build_effects(const HeroProgress *base,const Inventory *inventory,unsigned cls,ItemEffects *out);
unsigned item_equipment_mask(const Inventory *inventory,unsigned strength,unsigned magic,unsigned dexterity);
unsigned game_effective_stat(unsigned stat);
int game_spell_bonus(void);
enum { WEAR_MELEE,WEAR_BOW,WEAR_BLOCK,WEAR_HIT };
void game_wear_equipment(Game *game,unsigned event);
unsigned game_light_radius(void);
unsigned game_vision_radius(void);
typedef struct { int16_t hp,mana,max_mana,gold_reserved,potions; uint16_t reserved; uint32_t belt_reserved; uint64_t belt; HeroProgress progress; GameGold gold; Inventory inventory; } CharacterState;
_Static_assert(sizeof(CharacterState)==832,"completion preserves archived hero layout");
void game_items_recalc(void);
void game_items_select(void);
void item_appearance_reset(void);
unsigned item_render_appearance(const Inventory *bag,const HeroProgress *stats,unsigned cls);
void game_items_update(Game *game);
void game_restore_mana(Game *game,unsigned amount,int full);
unsigned game_character_max_hp(const CharacterState *hero);
unsigned game_attribute_damage(const HeroProgress *p,const ItemEffects *e,unsigned cls);
unsigned game_weapon_type(void);
unsigned game_melee_physical(unsigned kind,unsigned damage,uint32_t seed);
extern uint8_t game_attack_repeat[4]; /* Display-only continuation hint. */
unsigned game_attack_duration(int bow);
unsigned game_attack_hit_wait(unsigned cls,unsigned timing);
static inline unsigned game_attack_ticks(unsigned timing){return timing&31;}
static inline int game_attack_bow(unsigned timing){return (timing>>5)==4;}
unsigned game_recovery_duration(unsigned cls,int block);
unsigned game_block_chance(const Game *g,unsigned enemy_level);
void game_receive_melee(Game *g,unsigned enemy,unsigned damage,unsigned block_roll);
void game_monster_knockback(Game *g,unsigned enemy,int from_x,int from_y);
void game_melee_damage(Game *g,unsigned enemy,unsigned damage);
unsigned game_weapon_element_roll(uint32_t seed,int bow,unsigned element);
void game_weapon_hit(Game *g,unsigned enemy,unsigned physical,unsigned fire,unsigned lightning);
int game_melee_hit(const Game *game,unsigned enemy);
unsigned game_player_damage(Game *game,unsigned damage,unsigned element);
int game_item_action(Game *g,unsigned index,int drop);
int game_item_drag(Game *g,unsigned code);
int game_item_drag_index(unsigned owner);
void game_item_drag_reset(void);
uint32_t game_item_drag_hash(uint32_t h);
unsigned game_item_drag_wire(unsigned code);
int game_identify_item(Game *g,unsigned index);
Item item_from_drop(const Drop *drop);
Drop item_to_drop(const Item *item,unsigned x,unsigned y);
int game_item_pickup(Game *g,int index);
enum { HERO_ERROR_MANA,HERO_ERROR_SPACE,HERO_ERROR_COUNT };
/* Zero means no cue; otherwise1 + class*2 + reason. Local presentation only. */
extern volatile uint32_t hero_audio_event;
void game_hero_error(const Game *g,unsigned reason);
void game_item_monster_drop(Game *g,int index);
extern uint32_t item_sound_events[2];
void game_item_sound(const Game *g,unsigned kind,unsigned pickup,int x,int y);
int game_item_space(const Game *g,int x,int y);
int game_uses_bow(void);
unsigned game_armor(void);
extern HeroProgress hero_progress;
extern uint8_t monster_credit[MAX_ENEMIES];
#include "spells.h"
void game_progress_init(HeroProgress *p,unsigned cls);
int game_progress_valid(const CharacterState *h);
uint32_t game_next_experience(unsigned level);
unsigned game_stat_limit(unsigned cls,unsigned stat);
int game_spend_stat(Game *g,unsigned stat);
void game_gain_experience(Game *g,unsigned monster_level,unsigned exp);
void game_credit_hit(Game *g,int monster);
void game_credit_kill(Game *g,int monster);
unsigned game_weapon_damage(const Game *g);
unsigned game_bow_accuracy(unsigned owner);

unsigned game_hero_class(void);
int game_max_hp(void);
void game_create_hero(CharacterState *hero,unsigned cls);
unsigned game_belt_kind(const Game *g,unsigned slot);
unsigned game_belt_code(unsigned kind);
int game_belt_valid(uint64_t belt,int count);
void game_belt_remove(Game *g,unsigned slot);
int game_use_elixir(Game *g,unsigned kind);
int game_add_potion(Game *g,unsigned kind);
int game_use_potion(Game *g,unsigned type);
int game_drink_potion(Game *g,unsigned kind);
/* The Cathedral atlas plus floor families exceeds 255; not part of SRAM. */
enum { DUNGEON_VISUAL_EMPTY=65535 };
void game_dungeon_visuals(const Game *g,uint16_t *tiles,uint8_t *depth);
void game_seed_next(uint32_t seed);
void game_generate_dungeon(Game *g,uint32_t seed);
uint32_t game_floor_seed(uint32_t campaign,unsigned level);
void game_generate_next(Game *g);
void game_get_character(const Game *g,CharacterState *hero);
void game_apply_character(Game *g,const CharacterState *hero);
void game_clear_character(Game *g);
void game_new_campaign(Game *g,const CharacterState *hero);
int game_controller_step(Game *g,unsigned held,unsigned pressed);
/* Town: 9216 walk flags packed into 1152 bytes. Dungeon: the same arena
 * holds two floor draw lists. Scene load replaces the owner before play. */
extern uint8_t town_collision[];
static inline int town_walkable(unsigned x,unsigned y){
 unsigned i=y*96+x;return (town_collision[i>>3]>>(i&7))&1;
}
static inline void town_set_walkable(unsigned x,unsigned y,int value){
 unsigned i=y*96+x,mask=1u<<(i&7);
 town_collision[i>>3]=(town_collision[i>>3]&~mask)|(value?mask:0);
}
static inline void town_pack_collision(const uint8_t *raw){
 for(unsigned i=0;i<1152;i++){unsigned bits=0;for(unsigned j=0;j<8;j++)if(raw[i*8+j])bits|=1u<<j;town_collision[i]=(uint8_t)bits;}
}
void game_enter_town(Game *g,int fresh);
void game_enter_dungeon(Game *g);
int game_region_town(Game *g,int fresh);
int game_region_dungeon(Game *g);
#endif
void game_init(Game *g);
void game_step(Game *g, unsigned held, unsigned pressed);
int game_walkable(const Game *g,int x,int y);
void render_game(const Game *g,volatile uint16_t *pixels);
extern const uint16_t game_palette[32];
#endif

void game_dungeon_set_macro(Game *g,int x,int y,unsigned id);
void game_dungeon_set_stairs(Game *g);

void game_dungeon_fix_walls(uint8_t m[MACRO_W][MACRO_W],int legacy);
int game_dungeon_commit_macros(Game *g,uint8_t m[MACRO_W][MACRO_W]);
int game_dungeon_read_macros(const Game *g,uint8_t m[MACRO_W][MACRO_W]);

extern uint8_t *dungeon_workspace;
void game_dungeon_spawn(Game *g);

void game_ai_reset(void);
void game_pack_clear(void);
void game_pack_forget(unsigned i);
int game_pack_assign(const Game *g,unsigned member,unsigned leader);
void game_monsters_step(Game *g);
int game_monster_blocks(const Game *g,int x,int y,int except);
unsigned game_enemy_walk_ticks(const Game *g,int i);
unsigned game_enemy_walking(int i);
/* Charge/fade state shares existing per-monster AI bytes. */
int game_enemy_charging(const Game *g,int i);
int game_enemy_spawning(const Game *g,int i);
int game_enemy_hidden(const Game *g,int i);
int game_monster_damageable(const Game *g,int i);
int game_monster_missile_target(const Game *g,int i);
int game_monster_stonable(const Game *g,int i);
unsigned game_enemy_opacity(const Game *g,int i); /* 0 or8; native fade art */
unsigned game_enemy_fade_frame(int i);
void game_monster_reveal(Game *g,int i);
void game_monster_telekinesis(Game *g,int i);
uint32_t game_monsters_hash(uint32_t hash);
int game_spell_golem_at(int x,int y,unsigned damage);
int game_spell_golem_position(unsigned owner,int *x,int *y);

/* Stable serialized kind IDs: retain v026-v028 zombie=0/skeleton=1. */
#define MONSTER_TYPES 242
enum {MONSTER_MISSILE_ACID=37,MONSTER_MISSILE_MAGMA=38,MONSTER_MISSILE_FLAME=39};
#define MONSTER_HISTORY_TYPES 91
#include "../generated/monster71_meta.h"
static inline unsigned game_monster_ai(unsigned k){return k<MONSTER_TYPES?monster71_ai[k]:255;}
#define MONSTER_CORPSE_TYPES 242
#define MONSTER_FIEND 58
#define MONSTER_GLOOM 59
#define MONSTER_CAVE_VIPER 60
#define MONSTER_FIRE_DRAKE 61
#define MONSTER_GOLD_VIPER 62
#define MONSTER_AZURE_DRAKE 63
static inline int game_monster_bat(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_BAT;}
static inline int game_monster_snake(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_SNAKE;}
#include "monster_unique.h"
#define MONSTER_WARLORD 48
#define MONSTER_UNSEEN 49
#define MONSTER_GHARBAD 46
#define MONSTER_ZHAR 47
#define MONSTER_ADVOCATE 40
#define MONSTER_LAZARUS 41
#define MONSTER_RED_VEX 42
#define MONSTER_BLACK_JADE 43
#define MONSTER_DIABLO 44
#define REGION_BONE 20u
#define REGION_VILE 19u
#define REGION_WATER 18u
#define MONSTER_FLESH_BOW 37
#define MONSTER_CARVER_SWORD 38
#define MONSTER_DEVIL_SPEAR 39
#define REGION_KING 17u
#define MONSTER_KING 33
#define MONSTER_CORPSE_BOW 34
#define MONSTER_HORROR_BOW 35
#define MONSTER_BURNING_BOW 36
static inline int game_monster_archer(unsigned kind){return game_monster_ai(kind)==MONSTER_AI_SKELBOW||game_monster_ai(kind)==MONSTER_AI_GOATBOW;}
#define MONSTER_SNOTSPIL 29
#define MONSTER_BLUE_SPEAR 30
#define MONSTER_BLUE_SWORD 31
#define MONSTER_OVERLORD 32
#define MONSTER_ILLUSION_WEAVER 28
#define MONSTER_FIRE_CLAN_ARCHER 27
#define MONSTER_OBSIDIAN_LORD 26
#define MONSTER_HORNED_DEMON 23
#define MONSTER_BUTCHER 6
#define MONSTER_BLACK_KNIGHT 19
#define MONSTER_BLOOD_KNIGHT 22
/* Quest states follow QUEST_NOTAVAIL/INIT/ACTIVE/DONE. */
extern unsigned dungeon_level,butcher_quest;
static inline unsigned game_dungeon_depth(void){return dungeon_level==REGION_BONE?6:dungeon_level==REGION_KING?3:dungeon_level==REGION_WATER?2:dungeon_level==REGION_VILE?15:dungeon_level;}
extern uint32_t campaign_seed;
int game_region_floor(Game *g,unsigned level);
int game_region_shortcut(Game *g,unsigned level);
unsigned game_shortcut_mask(void);
int game_shortcut_target(const Game *g);
void game_shortcut_spawn(Game *g);
int game_wounded_present(void);
unsigned game_monster_roster(unsigned level, unsigned budget, uint32_t *seed, uint8_t out[MONSTER_TYPES]);

/* Prop IDs 1..3 retain their serialized meaning. */
enum { PROP_SARC=1, PROP_CHEST=2, PROP_BARREL=3, PROP_CHEST_MEDIUM=4, PROP_CHEST_LARGE=5, PROP_DOOR_L=6, PROP_DOOR_R=7, PROP_DECOR_FIRST=8, PROP_DECOR_LAST=19,PROP_ROCK_STAND=20,PROP_VALOR_BOOK=21,PROP_VALOR_PEDESTAL=22,PROP_BLIND_BOOK=23,PROP_LACHDANAN=24,PROP_SNOTSPIL=25,PROP_SIGN_CHEST=26,PROP_KING_ENTRY=27,PROP_KING_LEVER=28,PROP_KING_CRUX1=29,PROP_KING_CRUX2=30,PROP_KING_CRUX3=31,PROP_WATER_ENTRY=32,PROP_WATER_CANDLE=33,PROP_VILE_BOOK=34,PROP_VILE_CIRCLE=35,PROP_VILE_ALTAR=36,PROP_LAZ_STAND=37,PROP_VILE_ENTRY=38,PROP_DIABLO_LEVER=39,PROP_MUSHROOM_PATCH=40,PROP_BONE_ENTRY=41,PROP_BONE_BOOK=42,PROP_WARLORD_BOOK=43,PROP_BONE_TOME=44,PROP_BONE_LEVER=45,PROP_WAR_ARMOR=46,PROP_WAR_WEAPON=47 };
typedef struct { uint8_t count,items[3]; int8_t monster; } PropLoot;
void game_prop_loot(const Game *g,int i,PropLoot *out);
unsigned game_prop_reservations(const Game *g);
int game_open_prop(Game *g,int i);
int game_operate_prop(Game *g,int i);
void game_monster_reset(int i);
void game_fallen_fear(Game *g,int x,int y);
unsigned game_enemy_special(const Game *g,int i);
unsigned game_enemy_hit(int i);
void game_monster_hit(Game *g,int i);
void game_monster_damage_reaction(Game *g,int i,unsigned damage);
unsigned game_fallen_retreat(int i);
unsigned game_fallen_rally(int i);

/* -2..-65 encode props. An item sentinel keeps all128 drop slots available
 * without growing the saved int8 target or the co-op player record. */
enum { TARGET_ITEM=-66, TARGET_PORTAL=-67 };
int game_item_target(const Game *g);
int game_prop_target(const Game *g);
int game_prop_trapped(const Game *g,unsigned index);
int game_skill_item(Game *g,unsigned index);
void game_trap_damage(Game *g,unsigned damage,unsigned element);
int game_infravision(void);
int game_prop_direction(const Game *g,int i);
/* Native L3 hinges have the opposite approach axis to L1/L2. */
enum { PROP_L2_DOOR_L=48,PROP_L2_DOOR_R=49,PROP_L3_DOOR_L=50,PROP_L3_DOOR_R=51,PROP_EXPLOSIVE_BARREL=52,PROP_WALL_TRAP_FIRST=53,PROP_WALL_TRAP_LAST=58 };
/* The remaining six-bit object IDs retain the existing four-byte live and
 * three-byte archived layouts. Ordinary shrines/books are distinct from quests. */
enum { PROP_BRAZIER=59,PROP_SHRINE_L=60,PROP_SHRINE_R=61,PROP_BOOKCASE_L=62,PROP_BOOKCASE_R=63 };
/* Extended kinds use the unused coordinate range in the existing3-byte
 * archive. Live objects remain4 bytes; map coordinates stay below80. */
enum { PROP_BLOOD_FOUNTAIN=64,PROP_PURIFYING_FOUNTAIN=65,PROP_MURKY_FOUNTAIN=66,
 PROP_TEAR_FOUNTAIN=67,PROP_THEME_CANDLE=68,PROP_BOOKSTAND=69,
 PROP_SKEL_FIRE=70,PROP_THEME_BANNER_L=71,PROP_THEME_BANNER_M=72,
 PROP_THEME_BANNER_R=73,PROP_SKEL_BOOK=74,PROP_THEME_DECAP=75,
 PROP_BURNING_CROSS=76,PROP_ARMOR_STAND=77,PROP_WEAPON_RACK=78,
 PROP_GOAT_SHRINE=79,PROP_CAULDRON=80,PROP_SLAIN_HERO=81,PROP_STORY_BOOK=82,
 PROP_FLAME_X=83,PROP_FLAME_Y=84,PROP_FLAME_LEVER=85 };
static inline int game_prop_is_flame(unsigned k){return k==PROP_FLAME_X||k==PROP_FLAME_Y;}
static inline int game_prop_repeatable(unsigned k){return k==PROP_DOOR_L||k==PROP_DOOR_R||(k>=PROP_L2_DOOR_L&&k<=PROP_L3_DOOR_R)||k==PROP_STORY_BOOK||k==PROP_FLAME_LEVER;}
unsigned game_story_index(void);
static inline int game_prop_is_fountain(unsigned kind){return kind>=PROP_BLOOD_FOUNTAIN&&kind<=PROP_TEAR_FOUNTAIN;}
static inline int game_prop_is_large_fountain(unsigned kind){return kind==PROP_PURIFYING_FOUNTAIN||kind==PROP_MURKY_FOUNTAIN;}
static inline int game_prop_is_theme_decor(unsigned kind){return game_prop_is_flame(kind)||kind==PROP_THEME_CANDLE||kind==PROP_SKEL_FIRE||(kind>=PROP_THEME_BANNER_L&&kind<=PROP_THEME_BANNER_R)||kind==PROP_BURNING_CROSS;}
int game_environment_use(Game *g,int index);
void game_item_shrine(Game *g,unsigned type,uint32_t seed);
unsigned game_shrine_type(const Game *g,unsigned index);
const char *game_environment_name(const Game *g,unsigned index);
enum { GAME_BOOK_NOTICE_BASE=200 }; /* Encodes the dropped spell in the existing notice byte. */
const char *game_environment_notice(unsigned notice);
uint32_t game_shrine_loot_salt(const Game *g);
int game_shrine_chest_eligible(const Game *g,unsigned index);
static inline int game_prop_is_shrine(unsigned kind){return kind==PROP_SHRINE_L||kind==PROP_SHRINE_R;}
static inline int game_prop_is_bookcase(unsigned kind){return kind==PROP_BOOKCASE_L||kind==PROP_BOOKCASE_R;}
extern volatile uint32_t hazard_audio_event;
void game_hazard_sound(unsigned type); /* 0 trap, 1 exploding barrel, 2 fountain; cosmetic. */
void game_wall_traps_generate(Game *g,uint32_t seed);
void game_wall_traps_step(Game *g);
int game_wall_trap_linked(const Game *g,unsigned target);
static inline int game_prop_is_barrel(unsigned kind){return kind==PROP_BARREL||kind==PROP_EXPLOSIVE_BARREL;}
static inline int game_prop_is_wall_trap(unsigned kind){return kind>=PROP_WALL_TRAP_FIRST&&kind<=PROP_WALL_TRAP_LAST;}
static inline int game_prop_is_door(unsigned kind){return kind==PROP_DOOR_L||kind==PROP_DOOR_R||(kind>=PROP_L2_DOOR_L&&kind<=PROP_L3_DOOR_R);}
static inline int game_prop_door_axis(unsigned kind){return kind==PROP_DOOR_R||kind==PROP_L2_DOOR_R||kind==PROP_L3_DOOR_L;}
/* Containers may be ignored by target sight, never by movement collision. */
static inline int game_prop_is_target_container(unsigned kind){
 return kind==PROP_SARC||kind==PROP_CHEST||kind==PROP_CHEST_MEDIUM||kind==PROP_CHEST_LARGE||kind==PROP_SIGN_CHEST;
}
static inline int game_prop_blocks(const Prop *p,int x,int y){
 if(game_prop_is_large_fountain(p->kind))return x<=(int)p->x&&x>=(int)p->x-1&&y<=(int)p->y&&y>=(int)p->y-1;
 if(!p->kind||x!=p->x||game_prop_is_wall_trap(p->kind)||game_prop_is_flame(p->kind))return 0;
 /* Retail wall-mounted shrines and bookcases are pre-drawn, nonsolid
  * objects; the wall behind them supplies the blocking terrain. */
 if(game_prop_is_shrine(p->kind)||game_prop_is_bookcase(p->kind))return 0;
 if(p->kind==PROP_BONE_ENTRY||p->kind==PROP_KING_ENTRY||p->kind==PROP_WATER_ENTRY||p->kind==PROP_VILE_ENTRY||p->kind==PROP_VILE_CIRCLE)return 0;
 if(game_prop_is_door(p->kind))return !p->state&&y==p->y;
 if(p->kind==PROP_SARC)return y==p->y||y==(int)p->y-1;
 if((game_prop_is_barrel(p->kind)||p->kind==PROP_LACHDANAN||p->kind==PROP_SNOTSPIL)&&p->state)return 0;
 return y==p->y;
}

void game_quest_bookcase(Game *g,const Prop *p);
void game_quest_spawn(Game *g);
int game_quest_valor_use(Game *g,unsigned prop);
void game_banner_change(Game *g);
void game_king_killed(Game *g,const Enemy *e);
void game_king_population(Game *g);
void game_water_population(Game *g);
extern uint16_t quest_diablo_cells[4];
void game_vile_change(Game *g,uint16_t *tiles,uint8_t *depth);
void game_diablo_change(Game *g,uint16_t *tiles,uint8_t *depth);
void game_late_population(Game *g);
int game_late_use(Game *g,unsigned index);
void game_late_change(Game *g,uint16_t *tiles,uint8_t *depth);
void game_quest_npc_spawn(Game *g);
void game_quest_npc_step(Game *g);
int game_quest_npc_killed(Game *g,const Enemy *e);
int game_mushroom_use(Game *g,unsigned index);
int game_mushroom_killed(Game *g,const Enemy *e);
void game_finale_population(Game *g);
void game_finale_spawn(Game *g);
void game_finale_killed(Game *g,const Enemy *e);
void game_finale_restore(Game *g); /* Rebuild death presentation; no completion reward. */
int game_finale_use(Game *g,unsigned index);
int game_finale_step(Game *g);
int game_finale_talk(Game *g);
int game_finale_protected(unsigned kind);
int game_monster_cast(Game *g,unsigned enemy,unsigned spell);

int game_quest_entry(Game *g);
void game_king_change(Game *g,uint16_t *tiles,uint8_t *depth);
int game_king_use(Game *g,unsigned index);
int game_banner_use(Game *g,unsigned index);
int game_quest_extra_use(Game *g,unsigned prop);
void game_quest_enter(Game *g);
int game_quest_valor_room(int x,int y);
void game_quest_town(Game *g);
int game_quest_talk(Game *g,unsigned npc);
void game_quest_picked(Game *g,const Drop *drop);
int game_quest_pickable(const Game *g,const Drop *drop);
void game_activate_nearby(Game *g);

void game_props_step(Game *g);
unsigned game_prop_frame(const Game *g,int i);

unsigned game_explore_revision(void);
void game_explore_reset(void);
void game_explore_update(const Game *g);
int game_explored(const Game *g,int x,int y);
void game_explore_export(uint8_t *out);
void game_explore_import(const uint8_t *in);
void game_explore_reveal(void);

/* Shared with the future bow controller; selection does not fire a weapon. */
int game_target_visible(const Game *g,int i);
int game_ranged_target(const Game *g,int manual);

/* Transient projectiles are discarded on load/region change, not saved. */
enum { MAX_ARROWS=8, BOW_ATTACK_TICKS=12, BOW_RELEASE_WAIT=6 };
typedef struct { int16_t x,y,vx,vy; int8_t start_x,start_y,last_x,last_y; uint8_t life,owner,direction,reserved; uint16_t damage,fire,lightning; } Arrow;
extern Arrow game_arrows[MAX_ARROWS];
extern volatile unsigned bow_shots,bow_hits,bow_blocked,bow_misses;
void game_arrows_reset(void);
void game_bow_aim(const Game *g);
void game_bow_fire(Game *g);
int game_monster_arrow(Game *g,unsigned enemy);
void game_receive_arrow(Game *g,unsigned kind,int sx,int sy,unsigned damage,unsigned block_roll);
void game_arrows_step(Game *g);
uint32_t game_arrows_hash(uint32_t h);
void game_damage_enemy(Game *g,int i,int damage);
/* Trap/barrel damage preserves existing XP contributors without crediting the
 * currently selected hero merely because an environmental effect hit. */
void game_damage_enemy_environment(Game *g,int i,int damage,unsigned element);
extern uint32_t monster_history[MONSTER_HISTORY_TYPES];
typedef struct {
 uint8_t family,min_floor,max_floor,level; uint16_t hp_min,hp_max; uint8_t intelligence,hit,impact,damage_min,damage_max,armor,immune,resist,walk,hit_frames;
 uint16_t image_cost,xp;
 uint8_t attack_frames;
} MonsterDef;
extern const MonsterDef monster_defs[MONSTER_TYPES];
extern const uint8_t monster_undead[MONSTER_TYPES];
unsigned game_monster_variant(const Game *g,unsigned family);
unsigned game_monster_hp(unsigned kind,uint32_t roll);
unsigned game_monster_attack_ticks(unsigned kind);
unsigned game_monster_hp_bound(unsigned kind,int maximum);
unsigned game_monster_immunities(unsigned kind);
unsigned game_monster_resistances(unsigned kind);
void game_damage_enemy_element(Game *g,int i,int damage,unsigned element);
/* Floor remains are independent of reusable enemy slots and save snapshots. */
enum { CORPSE_TICKS=1200 };
typedef struct { uint16_t ticks; uint8_t x,y,kind,dir; } Corpse;
extern Corpse game_corpses[MAX_ENEMIES];
/* Derived presentation hint; excluded from archives and co-op hashes. */
extern unsigned game_animated_corpses;
void game_corpse_add(const Game *g,int i);
unsigned game_corpse_frame(const Corpse *c);
void game_corpses_step(void);

/* Ground entries stay in creation order; holes are compacted only on insertion. */
enum { TOWN_DROPS=40 };
const Drop *game_drop_notice(const Game *g);
int game_gold_drop(Game *g,unsigned amount);
int game_drop_put(Game *g,Drop drop);
int game_reward_pending(const Game *g);
int game_reward_candidate(const Game *g);
int game_reward_resolve(Game *g,int candidate,int accept);
void game_reward_tick(Game *g);
