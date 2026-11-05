#ifndef ITEMS_H
#define ITEMS_H
#include <stdint.h>
typedef int32_t GameGold;
#define GAME_GOLD_MAX 2000000000
#include "item_catalog.h"
/* Existing IDs stay ordered; appended equipment uses the retail catalog. */
#include "../generated/item_kinds.h"
/* Cosmetic event sentinel; never an inventory kind or serialized item. */
enum { ITEM_SOUND_GRAB=ITEM_COUNT };
enum { EQUIP_HEAD,EQUIP_RING1,EQUIP_RING2,EQUIP_NECK,EQUIP_HAND,EQUIP_SHIELD,EQUIP_CHEST,EQUIP_COUNT,
 BAG_CELLS=40,ITEM_SLOTS=47,ITEM_EMPTY=255 };
enum { ITEM_BOW=1,ITEM_TWOHAND=2 };
/* Equipment-only flags bit6 stores permanent physical shrine changes without
 * growing either16-byte inventory or ground records. On tagged non-staff armor
 * only, bit7 is the base AC sign; staff bit7 retains its starter-staff meaning.
 * Legacy records stay valid. */
enum { ITEM_PHYSICAL=64,ITEM_PHYSICAL_NEGATIVE=128 };
enum { INV_OK=1,INV_FULL,INV_REQUIREMENTS,INV_GROUND_FULL,INV_TOWN,INV_BUSY,INV_NO_IDENTIFY,INV_NO_SCROLL };
/* Explicit enchant word: prefix+1 bits0..6, suffix+1 bits7..13, bit14 unique (bits0..6 = unique+1, bits7..13 zero), bit15 identified. */
/* Non-staff max_charges is the high durability byte; staff retains charges. */
typedef struct {uint32_t seed;uint8_t kind,durability,pos,flags,charges,max_charges;uint16_t enchant,wear_loss,reserved;} Item;
typedef struct {Item items[ITEM_SLOTS];} Inventory;
typedef struct {
 const char *name;
 uint8_t slot,width,height,level,weight,durability,low,high,ac_low,ac_high,strength,magic,dexterity,flags;
} ItemDef;
extern const ItemDef item_defs[ITEM_COUNT];
extern Inventory hero_inventory;
#include "item_magic_catalog.h"
int item_power_supported(int power);
int item_enchant_valid(const Item *item);
int item_unidentified(const Item *item);
const ItemAffix *item_affix(const Item *item,unsigned suffix);
int item_affix_value(const Item *item,unsigned suffix);
int item_affix_secondary(const Item *item,unsigned suffix);
const UniqueItemDef *item_unique(const Item *item);
int item_unique_supported(unsigned id);
unsigned item_art_index(const Item *item);
unsigned item_sound_kind(const Item *item);
unsigned item_width(const Item *item);
unsigned item_height(const Item *item);
unsigned item_strength(const Item *item);
unsigned item_flags(const Item *item);
int item_has_power(const Item *item,int power);
int item_make_unique(Item *item,unsigned id);
enum { ITEM_INDESTRUCTIBLE=65535 };
unsigned item_durability(const Item *item);
void item_set_durability(Item *item,unsigned value);
unsigned item_original_durability(const Item *item);
unsigned item_max_durability(const Item *item);
unsigned item_weapon_low(const Item *item);
unsigned item_weapon_high(const Item *item);
unsigned item_power_count(const Item *item);
ItemPower item_power_at(const Item *item,unsigned index);
int item_power_value(const Item *item,unsigned index);
int item_bonus(const Item *item,unsigned category);
unsigned item_weapon_roll(uint32_t roll,unsigned attribute_bonus);
void item_generate_magic(Item *item,unsigned level,int only_good);
void item_generate_store_magic(Item *item,unsigned low,unsigned high);
void item_name(const Item *item,char out[64]);
Inventory *game_inventory(void);
extern uint8_t item_damage_min,item_damage_max,item_armor,item_weapon_flags;
void items_init(Inventory *bag,unsigned cls);
int items_valid(const Inventory *bag);
int items_grid(const Inventory *bag,uint8_t grid[BAG_CELLS]);
int items_add(Inventory *bag,Item item);
int item_ac(const Item *item);
int item_is_spell(const Item *item);
int item_is_staff(const Item *item);
unsigned item_magic(const Item *item);
int game_staff_slot(void);
int item_valid(const Item *item);
unsigned item_spell(const Item *item);
unsigned item_charge_multiplier(const Item *item);
unsigned item_weapon_art(const Inventory *bag,unsigned strength,unsigned magic,unsigned dexterity);
unsigned item_armor_art(const Inventory *bag,unsigned strength,unsigned magic,unsigned dexterity);
int item_scroll(const Item *item);
unsigned item_gold(uint32_t seed);
unsigned item_gold_amount(const Item *item);
unsigned item_candidate(uint32_t seed,unsigned level);
Item game_quest_loot(unsigned quest,uint32_t seed);
uint32_t item_seed(uint32_t seed);
Item item_create(unsigned kind,uint32_t seed);
Item item_create_level(unsigned kind,uint32_t seed,unsigned level);
/* Six index bits avoid the host's bits2..3 simulation clock. */
#define GOLD_COMMAND 2048u
#define GOLD_COMMAND_MASK 3584u
#define GOLD_COMMAND_MAKE(units) (GOLD_COMMAND|((units)&3u)|(((units)&60u)<<2))
#define ITEM_MOVE_COMMAND 2560u
#define ITEM_MOVE_MASK 3584u
#define ITEM_COMMAND 1024u
#define ITEM_DROP_COMMAND 2048u
#define ITEM_IDENTIFY_COMMAND 256u
#define ITEM_COMMAND_INDEX(v) (((v)&3u)|(((v)>>2)&60u))
#define ITEM_COMMAND_MAKE(i,drop) (ITEM_COMMAND|((i)&3u)|(((i)&60u)<<2)|((drop)?ITEM_DROP_COMMAND:0))
#endif
