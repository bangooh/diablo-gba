#include "game.h"
#include "performance_options.h"
#include "../generated/ai_specials.h"
#include "../generated/skeleton_spawn.h"
#include "world.h"
#include "../generated/bg_water_palette.h"
#include "globes.h"
#include "world_rooms.h"
#include "hazards.h"
#include "coop.h"
#include "link.h"
#include "audio.h"
#include "../generated/levelup.h"
#include "../generated/spell_assets.h"
#include "../generated/spell_expansion_assets.h"
static unsigned last_spell_icon=~0u,spell_audio_seen,hazard_audio_seen;
#include "../generated/combat_assets.h"
#include "../generated/cathedral_monsters.h"
#include "../generated/assets.h"
#include "../generated/packed_assets.h"
#include "../generated/door_frames.h"
#include "../generated/environment_props.h"
#include "../generated/fountain_props.h"
#include "../generated/explosive_barrels.h"
#include "../generated/wall_traps.h"
#include "../generated/prop_atlas.h"
#include "../generated/quest_objects71.h"
#include "../generated/environment76.h"
_Static_assert(RAW_PROP_FRAMES==PROP_REGIONAL_DOORS_BASE*4096,"regional door frame boundary");
_Static_assert(PROP_BARRELEX_BASE==PROP_REGIONAL_DOORS_BASE+PROP_REGIONAL_DOORS_FRAMES&&PROP_BARRELEX_LAST<PROP_WALL_TRAP_BASE&&PROP_WALL_TRAP_FRAME_LAST+1==PROP_BRAZIER_BASE&&PROP_ENVIRONMENT_FRAME_LAST+1==PROP_FOUNTAIN_BASE&&PROP_FOUNTAIN_FRAME_LAST+1==PROP_QUEST71_BASE&&PROP_QUEST71_FRAME_LAST+1==PROP_ENV76_BASE&&PROP_ENV76_FRAME_LAST+1==PROP_ATLAS_FRAMES,"prop atlas virtual frame range");
#include "../generated/dungeon_art.h"
extern const uint8_t asset_dungeon_art_raw[];
#include "../generated/fixed_wall_art.h"
#include "dungeon_doors.h"
#include "../generated/floor_variants.h"
#include "../generated/floor_actors.h"
#include "../generated/more_monsters.h"
#include "../generated/butcher_assets.h"
#include "../generated/bow_assets.h"
#include "../generated/ground_items.h"
#include "../generated/floor_decals.h"
#include "../generated/floor_tile_masks.h"
#include "../generated/heroes.h"
#include "../generated/towners.h"
#include "../generated/loading_town.h"
#include "lz.h"
#include "render_coords.h"
#include "actor_frames.h"
extern const uint8_t asset_hero_frame_pool[];
#include "../generated/cast_families_stage69.h"
#include "../generated/prop_highlight.h"
#include "../generated/unique_monsters.h"
#include "../generated/monsters_stage66.h"
#include "../generated/monsters_stage71.h"
typedef struct { const uint8_t *n,*w,*a,*palette; uint8_t idle,walk,attack,bank,bright; const char *name; } MonsterArt;
static const MonsterArt monster_art[MONSTER_TYPES]={
#include "../generated/monster71_art.inc"
};
typedef struct {
 const uint8_t *idle,*walk,*attack,*hit,*town_idle,*town_walk;
 uint8_t idle_count,walk_count,attack_count,hit_count,town_idle_count,town_walk_count,impact,bow;
} HeroArt;
#include "../generated/weapon_heroes.h"
#include "../generated/player_block_art.h"
static const uint8_t *const hero_palette[3]={asset_obj_palette,asset_rogue_palette,asset_sorcerer_palette};
typedef struct {const uint8_t *data;unsigned count;} HitArt;
static const HitArt monster_hit_art[MONSTER_TYPES]={
#include "../generated/monster71_hit.inc"
};
static const HitArt monster_special_art[MONSTER_TYPES]={
#include "../generated/monster71_special.inc"
};
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
Game game;
volatile uint32_t graphics_frames;
volatile uint32_t scene_loads;
/* D8 host-readable profiling, Timer0 ticks (1024 CPU cycles). No HUD writes. */
volatile uint16_t frame_profile[8];
static unsigned terrain_upload_count;
static uint16_t frame_deadline,frame_oversized;

static __attribute__((section(".text.ui"),noinline)) void profile_time(unsigned slot,unsigned start){
 unsigned ticks=(uint16_t)(audio_game_clock()-start);
 frame_profile[slot]=(uint16_t)ticks;
 if(ticks>frame_profile[slot+1])frame_profile[slot+1]=(uint16_t)ticks;
}
uint8_t town_collision[(MAX_ENEMIES+MAX_DROPS)*16] __attribute__((aligned(4)));
_Static_assert(sizeof(town_collision)>=1152,"town collision bitset capacity");
static uint16_t town_map[384*208] __attribute__((aligned(4)));
static uint32_t load_buffer[4672];
/* Actor raw-frame caches occupy bytes 0..10239. Sparse patch decoding uses
 * this otherwise idle part of the shared renderer/menu loading workspace. */
enum { ACTOR_SCRATCH_WORD=2560 };
_Static_assert(ACTOR_SCRATCH_WORD+512<=sizeof(load_buffer)/sizeof(load_buffer[0]),"actor scratch capacity");
/* All generators, including game_init, need the full arena. L1 visual
 * macro scratch uses its otherwise unused first1600 bytes. */
uint8_t *dungeon_workspace=(uint8_t*)town_map;
/* Fixed runtime arena boundary, independent of generated art stream size. */
enum { DUNGEON_LAYOUT_OFFSET = 80576 };
#define PROP_LOOKUP ((uint8_t*)town_map+120000)
_Static_assert(DUNGEON_LAYOUT_OFFSET+MAP_W*MAP_H*2+13*2048<=120000,"actor cache overlaps props");
/* Props read exact8x8 tiles directly from ROM. The former24KiB frame-cache
 * range126400..150975 is no longer needed; PROP_LOOKUP remains above. */
_Static_assert(GAME_ROOMS_LIVE_OFFSET+GAME_ROOMS_LIVE_BYTES<=sizeof(town_map),"room ID arena capacity");
_Static_assert(128000+25600<=sizeof(town_map),"generation scratch capacity");
static uint8_t prop_draw_frames[MAX_PROPS];
static int highlighted_prop=-1;
static int highlighted_drop=-1;
typedef struct {int16_t x,y;uint8_t foot8,id;uint16_t frame;} FloorDraw;
enum { FLOOR_CAPACITY=MAX_ENEMIES+MAX_DROPS };
/* Collision is town-only; dungeon floor lists reuse its region scratch. */
#define FLOOR_LIST(n) ((FloorDraw*)town_collision+(n)*FLOOR_CAPACITY)
_Static_assert(sizeof(FloorDraw)*FLOOR_CAPACITY*2<=sizeof(town_collision),"floor list capacity");
static unsigned floor_count[2],floor_list;
volatile unsigned floor_item_pixels,floor_masked_pixels;
static uint32_t item_sound_seen[2];
enum { WORLD_TILES=21*31 };
/* The town map is dungeon-only scratch. Native Cathedral art now stays in ROM.
 * Leave the L2/L3/L4 layout+depth at 0..19199 and actor/prop arenas intact. */
#define TERRAIN_CACHE ((uint8_t*)town_map+19200)
#define DEATH_CACHE (TERRAIN_CACHE+WORLD_TILES*64)
#define DEATH_KEYS ((uint16_t*)(DEATH_CACHE+4*4096))
/* Screen-relative 31-bit rows share the unused tail of the death-cache arena. */
#define FLOOR_COVERAGE ((uint32_t*)(DEATH_KEYS+4))
#define FLOOR_ANIMATED (FLOOR_COVERAGE+21)
#ifndef DEATH_SPRITES
#define DEATH_SPRITES 1
#endif
typedef struct {uint8_t count,first,indices[4],floor_start[4];uint16_t dirty;int cam_x,cam_y;uint16_t pending,displayed,commit_start,commit_end,overruns;int16_t presented_x,presented_y;} DeathSprites;
_Static_assert(sizeof(DeathSprites)%4==0,"death sprite state must preserve light-window alignment");
#define DEATH_SPRITE_STATE ((DeathSprites*)(FLOOR_ANIMATED+21))
#define presented_x (DEATH_SPRITE_STATE->presented_x)
#define presented_y (DEATH_SPRITE_STATE->presented_y)
#define DEATH_SPRITE_LUT ((uint8_t*)(DEATH_SPRITE_STATE+1))
#define DEATH_SPRITE_STAGE ((uint8_t*)town_map+126400)
_Static_assert(126400+12*2048<=GAME_ROOMS_LIVE_OFFSET,"staged actor tiles overlap room IDs");
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024<=DUNGEON_LAYOUT_OFFSET,"death sprite scratch overlaps dungeon art");
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4<=DUNGEON_LAYOUT_OFFSET,"terrain/death caches overlap L1 layout");
static int16_t ring_x[WORLD_TILES],ring_y[WORLD_TILES];
#if PERF_TERRAIN_ROWS
/* Dungeon-only spare arena word; never write it while town_map is town art.
 * Physical ring rows, independent of camera wrap. Interior invalidations must
 * mark the row; endpoints alone only prove unchanged camera coverage. */
#define terrain_dirty_rows (*(uint32_t*)((uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET-4))
#endif
static __attribute__((section(".text.ui"),noinline)) void terrain_dirty(unsigned slot){
#if PERF_TERRAIN_ROWS
 if(!game.area)terrain_dirty_rows|=1u<<(slot/31);
#endif
}
static __attribute__((section(".text.ui"),noinline)) void terrain_invalidate(unsigned slot){ring_x[slot]=RING_INVALID;terrain_dirty(slot);}
static uint8_t town_depth[WORLD_TILES+32][64] __attribute__((aligned(4)));
#define MAP_OUTLINE ((uint8_t*)town_depth+WORLD_TILES*64)
static uint32_t map_revealed_words[450];
#define MAP_REVEALED ((uint8_t*)map_revealed_words)
_Static_assert(WORLD_TILES*64+2000<=sizeof(town_depth),"map cache overlaps active depth");
volatile unsigned map_contour_builds;
static uint16_t depth_max[WORLD_TILES];

/*43 extra terrain tiles use spare panel cells, unused BG1 map rows and
 * seven status-frame cells. Menus restore those cells before using them. */
static inline unsigned world_tile(unsigned slot){
 if(slot<608)return slot;slot-=608;
 if(slot<4)return 512+181+slot;slot-=4;
 if(slot<19)return 512+226+slot;slot-=19;
 if(slot<12)return 884+slot;slot-=12;
 if(!slot)return 608;return 512+193+slot-1;
}
static unsigned hud_transparent=1;
volatile unsigned hero_occluded_pixels;

static uint16_t base_palette[512] __attribute__((aligned(4)));
static uint8_t gamma_level=2,death_red_ready,dpad_rotation;
static void actor_palette_refresh(void);
static __attribute__((section(".text.ui"),noinline)) void apply_gamma(void){
    for(int i=0;i<512;i++){
        unsigned c=base_palette[i],out=0;
        for(unsigned shift=0;shift<15;shift+=5){
            unsigned v=((c>>shift)&31)*(8+gamma_level)/10;
            if(v>31)v=31;out|=v<<shift;
        }
        R16(0x05000000+i*2)=(uint16_t)out;
    }
    actor_palette_refresh();
}
#include "motion.h"
typedef struct {uint16_t x_pal_shape,tile_size;int16_t y,depth;} Obj;
_Static_assert(sizeof(Obj)==8,"compact world OAM plan");
static Motion motion[MAX_ENEMIES+1],coop_motion[4];
enum { TOWN_MAP_NPC_OAM=52, TOWN_ACTOR_OAM=64 };
_Static_assert(TOWN_MAP_NPC_OAM+NPC_COUNT<=TOWN_ACTOR_OAM,"town minimap markers overlap actors");
static uint16_t oam[128*4] __attribute__((aligned(4)));
static Obj objects[84];
static int object_count,cam_x,cam_y;
static unsigned quest_tile_ready;
static int last_frame[13];
static const uint8_t *last_asset[13];
static int last_dir[13];
static int mask_x[13],mask_y[13],mask_complete[13],mask_cam_x[13],mask_cam_y[13];
static uint16_t actor_unmasked_slots,actor_clear_slots;
static uint16_t actor_hidden_slots;
static uint32_t crowd_mask_epoch;
static uint32_t actor_vram_tiles[13][2];
static int16_t actor_mask_foot[13];
static const uint8_t *raw_base[4];
static int raw_dir[4],raw_frame[4],raw_next;
static const uint8_t *d_raw_base[13];
static int d_raw_dir[13],d_raw_frame[13];
static uint8_t d_raw_ref[13];
static unsigned last_hud_tick;
#include "../generated/belt_tiles.h"
#include "../generated/belt_right_tiles.h"
enum { BELT_TILE=936, BELT_RIGHT_TILE=BELT_TILE+sizeof(belt_tiles)/32, MAP_ARROW_TILE=964, MAP_NPC_TILE=996 };
_Static_assert(BELT_TILE>=936&&BELT_RIGHT_TILE+sizeof(belt_right_tiles)/32<=MAP_ARROW_TILE,"belt overlaps spell effects or map arrows");
_Static_assert(MAP_ARROW_TILE+4*8<=MAP_NPC_TILE,"map marker overlap");
static uint32_t last_belt=~0u;
static uint8_t map_visible;
static int map_last_x=-1,map_last_y=-1,map_last_area=-1;
static unsigned map_pending,map_init=1;
static uint8_t map_screen_fallback,map_screen_live;
volatile unsigned frame_state; /* bit0 ready; upper bits committed frames */
static void frame_commit(void);
static unsigned map_line_color,map_last_revision;
volatile unsigned map_commits,map_commit_overruns;
volatile int map_ref_x,map_ref_y;
static void automap_commit(void);
static uint8_t map_stream_active;
static void automap_stream_draw(void);
static void automap_stream_commit(void);
static void automap_load_markers(void);
static void automap_prime(void);
static void automap_blend(void);
volatile unsigned map_unique_tiles,map_tile_overflows;
#define UI_FN __attribute__((section(".text.ui"),noinline))
#include "../generated/town_shortcuts.inc"
#include "../generated/town_well.inc"
#ifndef LIGHT_HARDWARE
#define LIGHT_HARDWARE 1
#endif
#define LIGHT_WINDOW_TABLES ((uint32_t (*)[161])(DEATH_SPRITE_LUT+1024))
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4<=DUNGEON_LAYOUT_OFFSET,"light windows overlap dungeon art");
/* Crowded BG deaths may borrow the otherwise idle atomic OBJ staging arena.
 * Keys live after both light tables; no extra static RAM is allocated. */
#define FLOOR_EXTRA_KEYS ((uint16_t*)(LIGHT_WINDOW_TABLES+2))
#define FLOOR_EXTRA_ENABLED (FLOOR_EXTRA_KEYS[6])
#define FLOOR_CACHE_SLOTS ((uint8_t*)(FLOOR_EXTRA_KEYS+7))
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4+7*2+FLOOR_CAPACITY<=DUNGEON_LAYOUT_OFFSET,"extra floor keys overlap dungeon art");
/* Raw frame presence bits share dungeon-only spare arena bytes. */
#define D_RAW_TILES ((uint32_t (*)[2])(((uintptr_t)(FLOOR_CACHE_SLOTS+FLOOR_CAPACITY)+3)&~(uintptr_t)3))
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4+7*2+FLOOR_CAPACITY+3+13*8<=DUNGEON_LAYOUT_OFFSET,"raw tile metadata overlaps layout");
/* Pending actor occupancy includes old tiles that must be erased. */
#define D_STAGE_TILES (D_RAW_TILES+13)
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4+7*2+FLOOR_CAPACITY+3+26*8<=DUNGEON_LAYOUT_OFFSET,"stage tile metadata overlaps layout");
#define D_PROP_KEYS ((uint32_t*)(D_STAGE_TILES+13))
#define SOLO_STAGE_USED (D_PROP_KEYS+MAX_PROPS)
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4+7*2+FLOOR_CAPACITY+3+26*8+MAX_PROPS*4+8<=DUNGEON_LAYOUT_OFFSET-4,"solo stage bitmap overlaps terrain row mask");
_Static_assert(19200+WORLD_TILES*64+4*4096+8+42*4+sizeof(DeathSprites)+1024+2*161*4+7*2+FLOOR_CAPACITY+3+26*8+MAX_PROPS*4<=DUNGEON_LAYOUT_OFFSET-4,"prop frame keys overlap terrain row mask");
#include "projectile_view.inc"
#include "lighting.inc"
#include "hardware_light.inc"
volatile uint8_t hero_render_variants[4];
static uint8_t hero_palette_form;
static UI_FN unsigned hero_variant(const Inventory *inventory,const HeroProgress *stats,unsigned cls);
static UI_FN void hero_appearance_prepare(void);
static UI_FN unsigned hero_palette_bank(unsigned cls);
static UI_FN unsigned monster_palette_bank(unsigned kind,unsigned selected);
static UI_FN void monster_palette_load(void);
static unsigned infravision_frame, actor_infra_tint;
#include "actor_palette.inc"
#include "water_palette.inc"
static uint8_t globe_colors[GLOBE_COLORS],globe_levels[2];
volatile unsigned globe_uploads;
static UI_FN void death_sprite_palette(void);
static UI_FN void death_sprite_plan(void);
static UI_FN int death_sprite_index(unsigned corpse);
static UI_FN void death_sprite_draw(void);
static UI_FN void death_sprite_invalidate_rect(int x,int y,int width,int height);
static UI_FN int death_sprite_stage_actor(unsigned slot,const void *pixels,const uint32_t changed[2]);
static int last_hp=-1,last_mana=-1,last_max_mana=-1;
static const char glyphs[]=HUD_GLYPHS;
/* Glyph index plus one; zero retains the original unsupported-byte space. */
static const uint8_t hud_glyph_index[256]={
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 37,0,0,0,0,41,0,0,0,0,0,0,0,40,0,38,
 27,28,29,30,31,32,33,34,35,36,39,0,0,0,0,0,
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
 16,17,18,19,20,21,22,23,24,25,26,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static int clamp(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
static UI_FN void item_sound_update(void){
 for(unsigned i=0;i<2;i++)if(item_sound_seen[i]!=item_sound_events[i]){item_sound_seen[i]=item_sound_events[i];audio_queue_item(item_sound_seen[i]&255,i);}
}
static UI_FN void boss_sound_update(void){
 if(!boss_audio_event||audio_boss_busy())return;
 for(unsigned id=0;id<BOSS_INTRO_COUNT;id++)if(boss_audio_event&(1u<<id)){
  boss_audio_event&=~(1u<<id);audio_boss_intro(id);return;
 }
}
static UI_FN void hero_sound_update(void){
 unsigned event=hero_audio_event;hero_audio_event=0;
 if(event&&event<=3*HERO_ERROR_COUNT)
  audio_hero_error((event-1)/HERO_ERROR_COUNT,(event-1)%HERO_ERROR_COUNT);
}
static void dma(const void *src,void *dst,unsigned bytes){
    __asm__ volatile("" ::: "memory"); /* DMA reads local buffers outside C execution. */
    R32(0x040000D4)=(uint32_t)src;R32(0x040000D8)=(uint32_t)dst;
    R32(0x040000DC)=0x84000000u|(bytes>>2);
}
#include "outer_mask.inc"
#include "effect_tiles.inc"
static UI_FN void unpack_to(const uint8_t *src,void *dst,unsigned bytes){
    unsigned n=lz_decode(src,load_buffer,sizeof(load_buffer));
    if(n==bytes)dma(load_buffer,dst,bytes);
}
/* The Mode4 loading bitmap occupies0000..95ff. Keep all scene uploads above it.
 * Font tiles0..31 live at9800;32..40 atC000, around the fixed HUD allocation. */
_Static_assert(RAW_TOWN_FONT==2624&&RAW_DUNGEON_FONT==2624,"font layout capacity");
#include "hud_colors.inc"
#include "durability_hud.inc"
/* Retain5x5 strokes; add a one-pixel lower-right gray shadow in the spare8x8 cell. */
#include "monster_palette.inc"
static UI_FN unsigned info_color(unsigned target){
 unsigned best=~0u,index=1;
 for(unsigned i=1;i<256;i++){if(!game.area&&!bg_static_index(i))continue;unsigned c=base_palette[i];int r=(int)(c&31)-(int)target,g=(int)((c>>5)&31)-(int)target,b=(int)((c>>10)&31)-(int)target;unsigned d=(unsigned)(r*r+g*g+b*b);if(d<best){best=d;index=i;}}
 return index;
}
/* Percent uses the otherwise unused leading font tile. Keep the existing
 * forty-character frontend font and all packed font sizes unchanged. */
static UI_FN void hud_percent_pixels(uint8_t *pixels,unsigned color){
 static const uint8_t rows[5]={200,208,32,88,152};
 unsigned ink=hud_color_index(color),shadow=info_color(8);
 for(unsigned i=0;i<64;i++)pixels[i]=0;
 for(unsigned pass=0;pass<2;pass++)for(unsigned y=0;y<5;y++)for(unsigned x=0;x<5;x++)
  if(rows[y]&(128u>>x))pixels[(y+(pass?1:2))*8+x+(pass?1:2)]=(uint8_t)(pass?ink:shadow);
}
static UI_FN void hud_percent_tile(void *destination,unsigned color){
 uint32_t pixels[16];hud_percent_pixels((uint8_t*)pixels,color);dma(pixels,destination,64);
}
static UI_FN void outline_info_font(void){
 uint8_t *p=(uint8_t*)load_buffer,bg=p[37*64];unsigned white=hud_color_index(HUD_WHITE),gray=info_color(8);
 for(unsigned tile=0;tile<41;tile++){
  uint8_t ink[64];for(unsigned i=0;i<64;i++){ink[i]=p[tile*64+i];p[tile*64+i]=0;}
  if(!tile)continue;
  for(int y=1;y<6;y++)for(int x=0;x<5;x++)if(ink[y*8+x]!=bg){
   p[tile*64+(y+1)*8+x+2]=(uint8_t)gray;
  }
  for(unsigned y=1;y<6;y++)for(unsigned x=0;x<5;x++)if(ink[y*8+x]!=bg)p[tile*64+y*8+x+1]=(uint8_t)white;
 }
}
/* Only the information panel uses these small glyphs during gameplay.
 * LEVEL/DIFFICULTY have a separate font. Ground-item panels contain names
 * only, so one cached ink change colors their wrapped rows without allocating
 * a second font bank or borrowing any automap/mask tile. */
static unsigned hud_font_ink;
static UI_FN void unpack_font(const uint8_t *src){
    if(lz_decode(src,load_buffer,sizeof(load_buffer))==2624){
        if(hud_transparent)outline_info_font();
        dma((uint8_t*)load_buffer+64,(void*)0x06009840,1984);
        dma((uint8_t*)load_buffer+2048,(void*)0x0600c000,576);
        hud_percent_tile((void*)0x06009800,HUD_WHITE);
        hud_font_ink=hud_transparent?hud_color_index(HUD_WHITE):0;
    }
}
static inline unsigned font_tile(unsigned glyph){
    if(glyph==40)return 96;
    unsigned n=97+glyph;return n<128?n:256+n-128;
}
static UI_FN void hud_font_color(unsigned color){
 if(!hud_transparent)return;
 if(color>HUD_RED)color=HUD_WHITE;
 /* The high byte caches the logical color too, avoiding a256-entry nearest
  * palette search for every unchanged name row and every ordinary HUD tick. */
 if((hud_font_ink>>8)==color)return;
 unsigned ink=hud_color_index(color),old=hud_font_ink&255;
 if(ink==old){hud_font_ink=ink|(color<<8);return;}
 for(unsigned glyph=0;glyph<41;glyph++){
  volatile uint16_t *tile=(volatile uint16_t*)(0x06008000+font_tile(glyph)*64);
  /* VRAM requires halfword writes; byte stores duplicate their value. */
  for(unsigned i=0;i<32;i++){
   unsigned v=tile[i],lo=v&255,hi=v>>8;
   if(lo==old)lo=ink;if(hi==old)hi=ink;
   unsigned next=lo|(hi<<8);if(next!=v)tile[i]=(uint16_t)next;
  }
 }
 hud_font_ink=ink|(color<<8);
}
/* Inventory hides BG0 and owns world tiles0..81 until it restores the scene.
 * Its white font remains at0..40; the one selected item color uses41..81.
 * This helper touches only the first2624 scratch bytes, leaving the staged
 * inventory map at load_buffer+17408 intact. The caller invalidates both
 * borrowed ranges before world rendering resumes. */
static UI_FN void hud_font_color_copy(void *destination,unsigned color){
 /* Town actor cache0 begins at scratch byte2048. The font decoder spans it;
  * discard the keys before reuse, including the ground-item delete menu. */
 for(unsigned i=0;i<4;i++)raw_base[i]=0;
 if(lz_decode(game.area?asset_town_font:asset_dungeon_font,load_buffer,sizeof(load_buffer))!=2624)return;
 outline_info_font();
 uint8_t *pixels=(uint8_t*)load_buffer;
 unsigned white=hud_color_index(HUD_WHITE),ink=hud_color_index(color);
 if(ink!=white)for(unsigned i=0;i<2624;i++)if(pixels[i]==white)pixels[i]=(uint8_t)ink;
 hud_percent_pixels(pixels,color);
 dma(load_buffer,destination,2624);
}
#include "../generated/dual_font.h"
#include "../generated/overlay_font.h"
static unsigned overlay_level=~0u;
static uint8_t overlay_fg,overlay_bg;
/* The panel center has no artwork. Reuse its first two rows for 40 large
 * glyphs; panel_map must map those cells to transparent tile128, never here.
 * This leaves the automap dictionary and loading bitmap allocation intact. */
static inline unsigned large_font_tile(unsigned glyph){return glyph<22?133+glyph:163+glyph-22;}
static UI_FN void unpack_large_font(void){
    overlay_level=~0u;
    if(lz_decode(game.area?asset_town_font_large:asset_dungeon_font_large,load_buffer,sizeof(load_buffer))==40*64){
        if(hud_transparent){uint8_t *p=(uint8_t*)load_buffer,bg=p[36*64];for(unsigned i=0;i<40*64;i++)if(p[i]==bg)p[i]=0;}
        dma(load_buffer,(void*)0x0600a140,22*64);
        dma((uint8_t*)load_buffer+22*64,(void*)0x0600a8c0,18*64);
    }
    if(lz_decode(game.area?asset_town_overlay_font:asset_dungeon_overlay_font,load_buffer,sizeof(load_buffer))==OVERLAY_FONT_BYTES){
        overlay_fg=((uint8_t*)load_buffer)[9];overlay_bg=((uint8_t*)load_buffer)[8];
        dma(load_buffer,(void*)0x0600b280,13*64);
        dma((uint8_t*)load_buffer+13*64,(void*)0x0600b7c0,OVERLAY_FONT_BYTES-13*64);
    }
}
#include "info_spacing.inc"
static UI_FN void hud_font_mode(unsigned transparent){
 if(!transparent)info_stream_stop();
 if(hud_transparent==transparent)return;
 /* Hide shared cells before menu titles overwrite the mask/map graphics. */
 if(!transparent)R16(0x04000000)&=(uint16_t)~0x0c00u;
 hud_transparent=transparent;unpack_font(game.area?asset_town_font:asset_dungeon_font);unpack_large_font();
 /* Gameplay lends the large menu glyph cells to the mask and automap. */
 map_last_x=-1;map_init=1;map_pending=0;
 if(transparent){outer_mask_load(game.area?7:3,game_light_radius());automap_prime();}
 for(unsigned i=608;i<WORLD_TILES;i++)terrain_invalidate(i);
 last_hud_tick=~0u;durability_hud_key=~0u;
}
static UI_FN void hud_gameplay(void){
 hud_font_mode(1);
 outer_mask_refresh(game.area?7:3,game_light_radius());
}
volatile unsigned loading_phase,loading_area,loading_starts;
static unsigned loading_screen_active;
/* Forced blank produces WHITE on GBA/VBA-M. Keep a black backdrop instead,
 * with all layers disabled and darkening held until complete graphics exist. */
static UI_FN void display_black(void){
    info_stream_stop();
    lighting_hardware_stop();
    unsigned line;do{line=R16(0x04000006);}while(line<160||line>=227);
    R16(0x04000054)=16;R16(0x04000050)=0x00ff;
    R16(0x04000000)=0;R16(0x05000000)=0;
}
static UI_FN void loading_step(unsigned percent,unsigned frames){
    unsigned line;do{line=R16(0x04000006);}while(line<160||line>220);
    unsigned color=loading_area==6?LOADING_TOWN_COLOR:loading_area==2?138:loading_area==3?254:43,y0=(loading_area==2||loading_area==3)?12:140;
    for(unsigned y=y0;y<y0+7;y++)for(unsigned x=20;x<20+percent*2;x+=2)
        R16(0x06000000+y*240+x)=(uint16_t)(color|(color<<8));
    loading_phase=percent;
    for(unsigned i=0;i<frames;i++){
        while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
    }
}
extern const uint8_t asset_loading_cut2[],asset_loading_cut2_palette[],asset_loading_cut3[],asset_loading_cut3_palette[],asset_loading_cut4[],asset_loading_cut4_palette[];
static UI_FN unsigned loading_destination(unsigned area,unsigned level){return area?1:level==REGION_BONE?3:(level==REGION_KING||level==REGION_VILE)?2:level==REGION_WATER?4:level>=13?5:level>=9?4:level>=5?3:level>=2?2:0;}
static UI_FN void loading_begin(unsigned destination){
    /* NEWGAME/LOADGAME into town use native Cutstart. Keep that original
     * selection when the shared scene loader subsequently requests town. */
    if(loading_screen_active&&(loading_area==destination||(destination==1&&loading_area==6)))return;
    loading_area=destination;display_black();
    const uint8_t *picture=loading_area==6?asset_loading_tristram:loading_area==5?asset_loading_cut4:loading_area==4?asset_loading_cut3:loading_area==3?asset_loading_cut2:loading_area==2?asset_loading_cutl1d:asset_loading_cuttt;
    const uint8_t *palette=loading_area==6?asset_loading_tristram_palette:loading_area==5?asset_loading_cut4_palette:loading_area==4?asset_loading_cut3_palette:loading_area==3?asset_loading_cut2_palette:loading_area==2?asset_loading_cutl1d_palette:asset_loading_cuttt_palette;
    lz_decode(picture,town_map,sizeof(town_map));dma(town_map,(void*)0x06000000,240*160);
    unpack_to(palette,(void*)0x05000000,512);
    /* Clear affine transform left by automap before Mode4 bitmap display. */
    R16(0x04000020)=256;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=256;
    R32(0x04000028)=0;R32(0x0400002c)=0;
    unsigned line;do{line=R16(0x04000006);}while(line<160||line>=227);
    R16(0x04000000)=0x0404;R16(0x04000050)=0;R16(0x04000054)=0;
    loading_screen_active=1;loading_starts++;loading_step(10,6);
}
/* Player-only +18% saturation, once per scene before saving base_palette.
 * Preserve neutral shades and palette index0 transparency; gamma remains an
 * independent final display adjustment. Monster/NPC/world banks are untouched. */
static UI_FN unsigned hero_palette_bank(unsigned cls){
 return !coop_active?0:cls==3?(game.area?2:11):cls==1?(game.area?1:6):cls==2?4:0;
}
static UI_FN void saturate_hero_palette(unsigned bank){
 for(unsigned i=1;i<16;i++){
  unsigned address=0x05000200+bank*32+i*2,c=R16(address),out=0;
  int luma=(77*(c&31)+150*((c>>5)&31)+29*((c>>10)&31)+128)>>8;
  for(unsigned shift=0;shift<15;shift+=5){
   int delta=(int)((c>>shift)&31)-luma;
   int value=luma+(delta*118+(delta>=0?50:-50))/100;
   out|=(unsigned)clamp(value,0,31)<<shift;
  }
  R16(address)=(uint16_t)out;
 }
}
/* L1 room scratch overlaps the tile destination and first actor frame.
 * Publish before drawing the tiles, then discard every affected cache key. */
static UI_FN void cathedral_rooms_refresh(void){
 uint8_t *scratch=dungeon_workspace;dungeon_workspace=(uint8_t*)town_map;
 game_rooms_prepare_l1(&game);game_rooms_publish();dungeon_workspace=scratch;
 for(unsigned i=0;i<13;i++)d_raw_base[i]=0;
}
/* One-shot L3/L4 handoff: native macro bytes remain in the existing loading
 * arena until scene expansion. Never cache across gameplay or coop staging.
 * The checksum rejects any intervening scratch reuse; fallback regenerates. */
static uint32_t cave_handoff_seed,cave_handoff_hash,cave_handoff_key;
static UI_FN uint32_t cave_macro_hash(void){
 const uint8_t *p=(const uint8_t*)town_map;uint32_t h=2166136261u;
 for(unsigned i=0;i<1600;i++)h=(h^p[i])*16777619u;return h;
}
static UI_FN unsigned cave_key(unsigned level){
 if(level>=5&&level<=7){unsigned q=level==5?9:level==6?14:8;return level|((game_campaign_layout()&&quest_progress.state[q])?256u:0u)|((quest_progress.stage[q]&63u)<<9);}
 if(level==13)return level|((game_campaign_layout()&&quest_progress.state[11])?256u:0u)|((quest_progress.stage[11]&63u)<<9);
 if(level==16)return level|((unsigned)quest_progress.finale_flags<<8);
 return level|((level==10&&game_campaign_layout()&&quest_progress.state[10])?256u:0u);
}
static UI_FN void cave_handoff_prepare(unsigned level,uint32_t seed){
 cave_handoff_key=0;
 if(coop_active||level<5||level>16)return;
 cave_handoff_seed=seed;cave_handoff_hash=cave_macro_hash();cave_handoff_key=cave_key(level);
}
static UI_FN int cave_handoff_take(uint32_t seed){
 unsigned key=cave_handoff_key;cave_handoff_key=0;
 return dungeon_level>=5&&dungeon_level<=16&&key==cave_key(dungeon_level)&&
        seed==cave_handoff_seed&&(game_rooms_candidate()||(coop_active&&game_room_at(0,0)>=0))&&cave_handoff_hash==cave_macro_hash();
}
/* Fixed cathedral wall halves, with the ground diamond preserved in art.
 * Door lintels are included above their leaf; open/closed variants retain
 * the same flags. Selection never depends on actors or destructible props. */
static UI_FN void fixed_walls_prepare(void){
 uint16_t *layout=(uint16_t*)((uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET);
 for(int y=1;y<MAP_H;y++)for(int x=1;x<MAP_W;x++){
  unsigned cell=y*MAP_W+x,v=layout[cell];
  if(v>=DART_COUNT||!dart_occludes[v])continue;
  unsigned flags=0;
  if(dart_door[v])flags=0xc000;
  else {
   /* The 80px wall extends across several cells behind its anchor.
    * Adjacent-only tests leave opaque halves in front of a diagonal path.
    * Resolve the full rear footprint once when loading the dungeon. */
   for(int back=0;back<5;back++){
    int bx=x-back,by=y-back;
    if(bx<0||by<0)break;
    if(back&&game.map[by][bx])flags|=0xc000;
    if(bx>0&&game.map[by][bx-1])flags|=0x4000;
    if(by>0&&game.map[by-1][bx])flags|=0x8000;
   }
  }
  layout[cell]=(uint16_t)(v|flags);
 }
}
/* Fixed 75% wall halves for L2/L3/L4. Reuse spare bits in the
 * renderer-only occlusion byte; never modify piece IDs or save data. */
static inline __attribute__((always_inline)) void fixed_world_wall_cell(int x,int y,unsigned reach){
 if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H)return;
 uint8_t *flags=(uint8_t*)town_map+12800;
  unsigned cell=y*MAP_W+x,base=flags[cell]&1,mask=0;
  if(base)for(unsigned back=0;back<reach;back++){
   int bx=x-(int)back,by=y-(int)back;if(bx<0||by<0)break;
   if(back&&game.map[by][bx])mask|=0xc0;
   if(bx>0&&game.map[by][bx-1])mask|=0x40;
   if(by>0&&game.map[by-1][bx])mask|=0x80;
  }
  unsigned p=PROP_LOOKUP[cell];
  /* A shut door leaf remains solid-looking; only its lintel is stippled. */
  if(base&&p&&game_prop_is_door(game.props[p-1].kind)&&!game.props[p-1].state)mask|=0x20;
  flags[cell]=(uint8_t)(base|mask);
}
static UI_FN void fixed_world_walls_prepare(void){
 if(game.area||dungeon_level<=4)return;
 unsigned reach=game_world_art(dungeon_level)->height/16;
 for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++)fixed_world_wall_cell(x,y,reach);
}
static unsigned closed_door_cull_valid,door_graph_state;
static UI_FN void door_graph_build(void);
static UI_FN void closed_door_prepare(void);
static UI_FN void town_items_reset(void);
static UI_FN void load_scene(void){
    frame_state&=~1u;closed_door_cull_valid=0;door_graph_state=0;
    town_items_reset();hero_palette_form=255;
    boss_audio_event=0;
    hero_audio_event=0;
    effect_tiles_invalidate();water_palette_seen=255;death_red_ready=0;
    game_water_colors_reset();
    hud_transparent=1;audio_stop();loading_begin(loading_destination(game.area,dungeon_level));
    if(game.area){
        cave_handoff_key=0;game_rooms_invalidate();
        lz_decode(asset_town_map,town_map,sizeof(town_map));
        lz_decode(asset_town_collision,load_buffer,sizeof(load_buffer));
        town_shortcuts_apply(town_map,(uint8_t*)load_buffer);
        town_well_apply(town_map);
        town_pack_collision((const uint8_t*)load_buffer);
        /* Native cow footprints, shared by local and linked simulation. */
        for(unsigned c=0;c<3;c++){
            unsigned x=npc_xy[9+c][0],y=npc_xy[9+c][1];
            int dx=c?-1:0,dy=c==1?0:-1;
            town_set_walkable(x,y,0);town_set_walkable(x+dx,y,0);
            town_set_walkable(x,y+dy,0);town_set_walkable(x+dx,y+dy,0);
        }
        town_set_walkable(24,32,!game_wounded_present());
        unpack_to(asset_town_palette,base_palette,RAW_TOWN_PALETTE);
        unpack_font(asset_town_font);
        for(int i=0;i<WORLD_TILES;i++){terrain_invalidate(i);ring_y[i]=RING_INVALID;}
        for(int i=0;i<1024;i++)R16(0x0600e000+i*2)=0;
        R16(0x04000008)=0x1c83; /* BG0 256x256 streamed ring. */
        R16(0x0400000A)=0x1B88; /* BG1 base2: panel128, split font96..127 and256..264. */

    }else{
        unpack_to(asset_bg_palette,base_palette,ASSET_BG_PALETTE_BYTES);
        for(unsigned i=0;i<4;i++)DEATH_KEYS[i]=65535;
        FLOOR_EXTRA_ENABLED=0;
        for(int i=0;i<MAX_PROPS;i++)prop_draw_frames[i]=255;
        for(int i=0;i<6400;i++)PROP_LOOKUP[i]=0;
        for(int i=0;i<MAX_PROPS;i++)if(game.props[i].kind)PROP_LOOKUP[game.props[i].y*80+game.props[i].x]=i+1;
        highlighted_prop=-1;
        if(dungeon_level<=4){cathedral_rooms_refresh();game_dungeon_visuals(&game,(uint16_t*)((uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET),0);}
        else {uint8_t *scratch=dungeon_workspace;dungeon_workspace=(uint8_t*)town_map;uint32_t seed=game_floor_seed(campaign_seed,dungeon_level);if(cave_handoff_take(seed)){game_world_visuals_prepared(town_map,(uint8_t*)town_map+12800);if(game_rooms_candidate())game_rooms_publish();}else{game_world_visuals(town_map,(uint8_t*)town_map+12800,seed);game_rooms_publish();}dungeon_workspace=scratch;game_world_doors_overlay(&game,town_map,(uint8_t*)town_map+12800);}
        if(dungeon_level<=4)fixed_walls_prepare();else fixed_world_walls_prepare();
        door_graph_build();closed_door_prepare();
        unpack_font(asset_dungeon_font);
        for(int i=0;i<WORLD_TILES;i++){terrain_invalidate(i);ring_y[i]=RING_INVALID;}
        for(int i=0;i<1024;i++)R16(0x0600e000+i*2)=0;
        R16(0x04000008)=0x1c83;R16(0x0400000A)=0x1B88;
    }
    loading_step(coop_active?80:40,6);
    /* Procedural globes replace the old panel and all136 stored fill images. */
    for(unsigned i=0;i<RAW_CONTROL_PANEL_TOWN/4;i++)load_buffer[i]=0;
    dma(load_buffer,(void*)0x0600a000,RAW_CONTROL_PANEL_TOWN);
    for(int i=0;i<1024;i++)R16(0x0600D800+i*2)=128;
    for(int y=0;y<4;y++)for(int x=0;x<30;x++)R16(0x0600d800+((y+16)*32+x)*2)=(uint16_t)(x>=4&&x<26?128:129+y*30+x);
    unpack_large_font();
    unpack_to(asset_obj_palette,(void*)0x05000200,ASSET_OBJ_PALETTE_BYTES);
    if(!coop_active&&game_hero_class())unpack_to(hero_palette[game_hero_class()],(void*)0x05000200,32);
    if(game.area)unpack_to(asset_npc_palette,(void*)0x050002C0,RAW_NPC_PALETTE);
    else monster_palette_load();
    if(coop_active)for(unsigned cls=1;cls<3;cls++)unpack_to(hero_palette[cls],(void*)(0x05000200+hero_palette_bank(cls)*32),32);
    /* In co-op the unused town bank2 / unselected dungeon highlight bank11
     * holds Butcher independently of class, including mixed same-class heroes. */
    if(coop_active){lz_decode(asset_m71_pal_6,load_buffer,sizeof(load_buffer));dma(load_buffer,(void*)(0x05000200+hero_palette_bank(3)*32),32);}
    saturate_hero_palette(0);
    if(coop_active)for(unsigned cls=1;cls<3;cls++)saturate_hero_palette(hero_palette_bank(cls));
    unpack_to(asset_stairs,(void*)(0x06010000+880*32),RAW_STAIRS);
    unpack_to(asset_bow_palette,(void*)0x05000260,32);
    unpack_to(asset_bow_arrows,(void*)(0x06010000+896*32),2048);
    quest_tile_ready=0;highlighted_drop=-1;floor_count[0]=floor_count[1]=floor_list=0;
    automap_load_markers();
    last_spell_icon=~0u;spell_audio_seen=spell_audio_event;hazard_audio_seen=hazard_audio_event;
    dma(firebolt_tiles,(void*)(0x06010000+868*32),sizeof(firebolt_tiles));
    dma(levelup_tiles,(void*)(0x06010000+1000*32),sizeof(levelup_tiles));
    /* Shared solid32px-tall translucent panel. */
    for(unsigned i=0;i<256;i++)load_buffer[i]=0xffffffffu;dma(load_buffer,(void*)(0x06010000+832*32),1024);
    R16(0x05000200+(5*16+1)*2)=31;
    R16(0x05000200+(5*16+2)*2)=(31<<10)|(8<<5);
    R16(0x05000200+(5*16+3)*2)=5;R16(0x05000200+(5*16+4)*2)=5<<10;
    for(int i=256;i<512;i++)base_palette[i]=R16(0x05000000+i*2);
    last_hp=-1;last_belt=~0u;durability_hud_key=~0u;
    const uint16_t ui_colors[16]={0,4|(4<<5)|(5<<10),10|(10<<5)|(11<<10),20|(19<<5)|(15<<10),
        8,18,31|(3<<5),4<<10,18<<10,3|(8<<5)|(31<<10),31|(26<<5),31|(30<<5)|(20<<10),
        2|(2<<5)|(3<<10),31|(31<<5)|(31<<10),20|(20<<5)|(20<<10),0};
    for(int i=0;i<16;i++){R16(0x050002a0+i*2)=ui_colors[i];base_palette[256+80+i]=ui_colors[i];}
    durability_hud_palette();
    loading_step(80,6);loading_step(100,8);
    display_black();loading_screen_active=0;
    base_palette[256+15*16+1]=31|(15<<5); /* Orange automap player. */
    base_palette[256+15*16+2]=(31<<5)|(31<<10);
    base_palette[256+15*16+3]=31<<5;base_palette[256+15*16+4]=31|(31<<10);
    if(game.area){lz_decode(asset_ground_item_palette,load_buffer,sizeof(load_buffer));for(unsigned i=6;i<16;i++)base_palette[256+15*16+i]=((uint16_t*)load_buffer)[i];}
    if(!game.area)for(unsigned i=0;i<10;i++)base_palette[256+15*16+6+i]=spell_native_colors[i];
    base_palette[256+15*16+5]=5|(31<<5)|(7<<10); /* NPC green. */
    unsigned best=~0u;
    for(unsigned i=1;i<256;i++){
        if(!game.area&&!bg_static_index(i))continue;
        unsigned c=base_palette[i];int r=(c&31)-16,g=((c>>5)&31)-15,b=((c>>10)&31)-10;
        unsigned distance=(unsigned)(r*r+g*g+b*b);
        if((i&15)&&distance<best){best=distance;map_line_color=i;}
    }
    /* World continues behind the conditional transparent information panel. */
    R16(0x04000040)=240;R16(0x04000044)=160;
    R16(0x04000048)=0x0037;R16(0x0400004a)=0x0032;
    base_palette[0]=0; /* Empty world pixels use a black backdrop. */
    if(!game.area)water_palette_apply(game_water_palette_state(dungeon_level));
    globe_palette(globe_colors,base_palette,game.area?0:bg_static_slots);globe_levels[0]=globe_levels[1]=255;
    actor_palette_reset();lighting_palette();death_sprite_palette();
    /* Spare arrow-bank entries hold flat HUD colors. Build the death lookup
     * first so its existing pixel translation remains unchanged. */
    for(unsigned i=0;i<SPELL_FLAT_COLOR_COUNT;i++)base_palette[256+SPELL_FLAT_PALETTE_BANK*16+SPELL_FLAT_FIRST_COLOR+i]=spell_flat_colors[0][i];
    apply_gamma();loading_phase=0;
    outer_mask_load(game.area?7:3,game_light_radius());map_last_x=-1;map_init=1;map_pending=0;
    scene_loads++;
    audio_start(audio_region_track(game.area,game_dungeon_depth()));
}
static UI_FN void reveal_scene(void){
    while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
    frame_commit();automap_commit();
    R16(0x04000054)=0;automap_blend();
}
#include "town_items.inc"
static UI_FN void stream_town(void){
    TownItemDraw items[TOWN_DROPS+5];unsigned item_count=town_items_prepare(items),item_ready=0;uint8_t item_lut[2][16];
    int tx=render_tile(cam_x),ty=render_tile(cam_y);
    for(int y=ty;y<=ty+20;y++)for(int x=tx;x<=tx+30;x++){
        int slot=render_slot(x,y);
        if(ring_x[slot]!=x || ring_y[slot]!=y){
            unsigned valid=(unsigned)x<384&&(unsigned)y<208;
            unsigned bytes=valid?lz_block(asset_town_tiles,town_map[y*384+x],load_buffer,sizeof(load_buffer)):0;
            if(bytes==128){
                for(int p=0;p<64;p++)town_depth[slot][p]=((uint8_t*)load_buffer)[64+p];
            }else{
                for(unsigned p=0;p<16;p++)((uint32_t*)load_buffer)[p]=((uint32_t*)town_depth[slot])[p]=0;
            }
            if(item_count)town_items_overlay(items,item_count,x*8,y*8,slot,(uint8_t*)load_buffer,&item_ready,item_lut);
            dma(load_buffer,(void*)(0x06000000+world_tile(slot)*64),64);
            depth_max[slot]=0;
            for(int p=0;p<64;p++)if(town_depth[slot][p] && y*8+town_depth[slot][p]>depth_max[slot])depth_max[slot]=(uint16_t)(y*8+town_depth[slot][p]);
            ring_x[slot]=x;ring_y[slot]=y;
            R16(0x0600e000+((y&31)*32+(x&31))*2)=(uint16_t)world_tile(slot);
        }
    }
}

static inline const uint8_t *prop_tile(unsigned frame,unsigned tile){
 if(frame>=PROP_ATLAS_FRAMES||tile>=64)return 0;
 unsigned id=prop_atlas_tiles[frame][tile];
 return id?asset_prop_atlas+id*64:0;
}
static UI_FN void floor_invalidate(const FloorDraw *d){
 const uint8_t *picture=asset_floor_decals+((const uint32_t*)asset_floor_decals)[d->frame+1];
 if(!picture[1])return;
 int left=d->x,top=d->y+picture[0],right=left+63,bottom=top+picture[1]-1;
 if(d->frame>=FLOOR_ITEM_BASE){left+=16;right-=16;if(top<d->y+48)top=d->y+48;}
 unsigned exact=d->frame<FLOOR_ITEM_BASE&&!((d->x|d->y)&7);
 int origin_x=render_tile(d->x),origin_y=render_tile(d->y);
  for(int ty=render_tile(top);ty<=render_tile(bottom);ty++)for(int tx=render_tile(left);tx<=render_tile(right);tx++){
  if(exact&&!(floor_tile_masks[d->frame][ty-origin_y]&(1u<<(tx-origin_x))))continue;
  int slot=render_slot(tx,ty);
  if(ring_x[slot]==tx&&ring_y[slot]==ty){
   /* Settled remains/loot are baked into the unshaded tile. Remove the old
    * overlay by rebuilding geometry only when that composite changes. */
   if(light_tiles[slot]&16)terrain_invalidate(slot);else {light_tiles[slot]|=8;terrain_dirty(slot);}
  }
 }
}
#include "floor_decode.h"
/* Keep three dedicated corpse pages. The fourth stages animated terrain
 * without destroying its raw geometry. Only extra pages3/4 survive actor and
 * frame-map uploads; borrowing the other pages caused repeated decoding. */
enum { FLOOR_BASE_CACHE=3 };
#define FLOOR_CACHE_PINNED DEATH_KEYS[3]
static UI_FN unsigned floor_extra_prepare(void){
 const DeathSprites *s=DEATH_SPRITE_STATE;
 if(coop_active||!DEATH_SPRITES||s->count||s->pending||s->displayed){FLOOR_EXTRA_ENABLED=0;return 3;}
 if(!FLOOR_EXTRA_ENABLED)for(unsigned i=3;i<5;i++)FLOOR_EXTRA_KEYS[i]=65535;
 FLOOR_EXTRA_ENABLED=1;return 5;
}
static UI_FN unsigned floor_cache_key(unsigned slot){return slot<3?DEATH_KEYS[slot]:FLOOR_EXTRA_KEYS[slot];}
static UI_FN uint8_t *floor_cache_pixels(unsigned slot){return slot<3?DEATH_CACHE+slot*4096:DEATH_SPRITE_STAGE+slot*4096;}
static UI_FN void floor_cache_resolve(const FloorDraw *list,unsigned count,unsigned capacity){
 FLOOR_CACHE_PINNED=0;
 for(unsigned i=0;i<count;i++){
  unsigned slot=255;
  if(list[i].frame<FLOOR_ITEM_BASE)for(unsigned j=0;j<capacity;j++){
   if(floor_cache_key(j)==list[i].frame){slot=j;FLOOR_CACHE_PINNED|=1u<<j;break;}
  }
  FLOOR_CACHE_SLOTS[i]=(uint8_t)slot;
 }
}
static UI_FN const uint8_t *floor_cache_get(unsigned index){
 unsigned slot=FLOOR_CACHE_SLOTS[index];
 if(slot!=255)return floor_cache_pixels(slot);
 const FloorDraw *list=FLOOR_LIST(floor_list);unsigned frame=list[index].frame;
 if(frame>=FLOOR_ITEM_BASE)return 0;
 unsigned capacity=FLOOR_EXTRA_ENABLED?5:3;
 for(slot=0;slot<capacity;slot++)if(!(FLOOR_CACHE_PINNED&(1u<<slot))){
  uint8_t *pixels=floor_cache_pixels(slot);
  floor_decode(asset_floor_decals,frame,pixels);
  if(slot<3)DEATH_KEYS[slot]=(uint16_t)frame;else FLOOR_EXTRA_KEYS[slot]=(uint16_t)frame;
  FLOOR_CACHE_PINNED|=1u<<slot;
  for(unsigned i=0;i<floor_count[floor_list];i++)if(list[i].frame==frame)FLOOR_CACHE_SLOTS[i]=(uint8_t)slot;
  return pixels;
 }
 return 0;
}
/* Conservative per-screen-tile coverage keeps lighting-only updates O(1).
 * Animated tiles retain geometry-only caches, avoiding a rebuild each frame. */
static UI_FN void floor_coverage(void){
 for(unsigned i=0;i<42;i++)FLOOR_COVERAGE[i]=0;
 const FloorDraw *list=FLOOR_LIST(floor_list);int sx=render_tile(cam_x),sy=render_tile(cam_y);
 for(unsigned i=0;i<floor_count[floor_list];i++){
  const FloorDraw *d=&list[i];const uint8_t *picture=asset_floor_decals+((const uint32_t*)asset_floor_decals)[d->frame+1];
  if(!picture[1])continue;
  if(d->frame<FLOOR_ITEM_BASE&&!((d->x|d->y)&7)){
   int col=render_tile(d->x)-sx,row=render_tile(d->y)-sy;
   if(col<=-8||col>30)continue;
   for(unsigned y=0;y<8;y++)if(row+(int)y>=0&&row+(int)y<=20){
    unsigned bits=floor_tile_masks[d->frame][y],mask=col<0?bits>>-col:bits<<col;
    mask&=0x7fffffffu;FLOOR_COVERAGE[row+y]|=mask;
    if(d->frame>=MONSTER_CORPSE_TYPES*8)FLOOR_ANIMATED[row+y]|=mask;
   }
   continue;
  }
  int left=d->x,top=d->y+picture[0],right=left+63,bottom=top+picture[1]-1;
  if(d->frame>=FLOOR_ITEM_BASE){left+=16;right-=16;if(top<d->y+48)top=d->y+48;}
    left=render_tile(left)-sx;right=render_tile(right)-sx;top=render_tile(top)-sy;bottom=render_tile(bottom)-sy;
  if(left<0)left=0;if(right>30)right=30;if(top<0)top=0;if(bottom>20)bottom=20;
  if(left>right||top>bottom)continue;
  uint32_t mask=((1u<<(right-left+1))-1)<<left;
  unsigned animated=d->frame>=MONSTER_CORPSE_TYPES*8&&d->frame<FLOOR_ITEM_BASE;
  for(int row=top;row<=bottom;row++){FLOOR_COVERAGE[row]|=mask;if(animated)FLOOR_ANIMATED[row]|=mask;}
 }
}
#include "floor_dirty.inc"
static UI_FN void floor_prepare(void){
 death_sprite_plan();
 unsigned capacity=floor_extra_prepare();
 unsigned next=floor_list^1,n=0;FloorDraw *out=FLOOR_LIST(next),*old=FLOOR_LIST(floor_list);
 highlighted_drop=!game.attack_wait&&game.target==TARGET_ITEM?game_item_target(&game):-1;
 /* All remains precede all items, so loot is always readable above a body. */
 for(int i=coop_active?MAX_ENEMIES:0;i<MAX_ENEMIES+MAX_DROPS;i++){
  int x,y,lift=0;unsigned frame;
  if(i<MAX_ENEMIES){
   const Corpse *c=&game_corpses[i];if(!c->ticks||c->kind>=MONSTER_TYPES)continue;
   int selected=death_sprite_index(i);if(selected>=0){DEATH_SPRITE_STATE->floor_start[selected]=(uint8_t)n;continue;}
   x=c->x;y=c->y;frame=game_corpse_frame(c);
  }else{
   int index=i-MAX_ENEMIES;const Drop *d=&game.drops[index];
   if(!d->active||d->kind>=ITEM_COUNT)continue;
   x=d->x;y=d->y;if(game_quest_pickable(&game,d))lift=10;Item art_item=item_from_drop(d);frame=(index==highlighted_drop?FLOOR_BRIGHT_BASE:FLOOR_ITEM_BASE)+ground_item_image[item_art_index(&art_item)];
  }
  int wx=DUNGEON_ORIGIN+(x-y)*16-32,wy=72+(x+y)*8-64-lift;
  if(wx+64<=cam_x||wx>=cam_x+248||wy+64<=cam_y||wy>=cam_y+168)continue;
  out[n++]=(FloorDraw){wx,wy,(wy+64+lift)/8,(uint8_t)i,(uint16_t)frame};
 }
 floor_list_invalidate(out,n,old,floor_count[floor_list]);
 floor_count[next]=n;floor_list=next;floor_coverage();
 /* Existing visible frames stay pinned. Decode a missing frame only if
  * an intersecting dirty tile actually needs it in floor_overlay(). */
 floor_cache_resolve(out,n,capacity);
}
/* Native quest progression changes only dedicated hardware palette slots. */
static UI_FN void water_refresh(unsigned previous_state){
 if(game_water_colors_step(&game,previous_state)){
  if(previous_state!=3)audio_quest_complete();
  water_palette_apply(game_water_palette_state(dungeon_level));
 }
}
static __attribute__((noinline)) void floor_blit(const uint8_t *cached,int x,int y,int top,int bottom,int py,int slot,int foot,uint8_t *pixels){
 int left=x<0?-x:0,right=x+8>64?64-x:8;
 for(int row=top;row<bottom;row++)for(int col=left;col<right;col++){
  unsigned target=row*8+col,c=cached[(y+row)*64+x+col],depth=town_depth[slot][target];
  if(c){if(!depth||py+(int)depth<=foot)pixels[target]=(uint8_t)c;else floor_masked_pixels++;}
 }
}
/* An aligned body tile entirely in front of terrain needs only alpha merge.
 * Keep this cold-path entry in ROM; existing masked pixel code stays in IWRAM. */
static UI_FN void floor_blit_clear(const uint8_t *cached,int x,int y,int top,int bottom,uint8_t *pixels){
 for(int row=top;row<bottom;row++){
  const uint32_t *src=(const uint32_t*)(cached+(y+row)*64+x);
  uint32_t *out=(uint32_t*)(pixels+row*8);
  for(unsigned p=0;p<2;p++){
   unsigned v=src[p];if(!v)continue;
   unsigned high=(((v&0x7f7f7f7fu)+0x7f7f7f7fu)|v)&0x80808080u;
   unsigned mask=(high>>7)*255u;out[p]=(out[p]&~mask)|v;
  }
 }
}
static UI_FN void floor_overlay(int px,int py,int slot,uint8_t *pixels){
 const FloorDraw *list=FLOOR_LIST(floor_list);
 const uint32_t *offsets=(const uint32_t*)asset_floor_decals+1;
 for(unsigned i=0;i<floor_count[floor_list];i++){
  const FloorDraw *d=&list[i];int x=px-d->x,y=py-d->y;
  if(x<=-8||x>=64||y<=-8||y>=64)continue;
  if(d->frame<FLOOR_ITEM_BASE&&!((x|y)&7)&&!(floor_tile_masks[d->frame][y/8]&(1u<<(x/8))))continue;
  if(d->frame>=FLOOR_ITEM_BASE&&(x<=8||x>=48||y<48))continue;
  const uint8_t *picture=asset_floor_decals+offsets[d->frame];
  int top=picture[0]-y,bottom=picture[0]+picture[1]-y;
  if(top<0)top=0;if(bottom>8)bottom=8;if(top>=bottom)continue;
  const uint8_t *cached=floor_cache_get(i);
  if(cached){
   if(x>=0&&x<=56&&!(x&3)&&depth_max[slot]<=(unsigned)(d->foot8*8))floor_blit_clear(cached,x,y,top,bottom,pixels);
   else floor_blit(cached,x,y,top,bottom,py,slot,(d->foot8*8),pixels);
   continue;
  }
  const uint8_t *body=asset_floor_decals+*(const uint32_t*)(picture+8);
  const uint16_t *rows=(const uint16_t*)body;
  unsigned bits=picture[2],mask=(1u<<bits)-1;const uint8_t *palette=asset_floor_decals+*(const uint32_t*)(picture+4);
  for(int row=0;row<8;row++){
   int iy=y+row;if(iy<picture[0]||iy>=picture[0]+picture[1])continue;
   const uint8_t *p=body+rows[iy-picture[0]]-8;int col=0;
   while(col<x+8){
    unsigned run=*p++;if(!run)break;
    if(run>=128){col+=256-run;continue;}
    unsigned repeat=run>64;if(repeat)run-=64;
    int end=col+(int)run,left=col>x?col:x,right=end<x+8?end:x+8;
    for(int ix=left;ix<right;ix++){
     int target=row*8+ix-x;unsigned depth=town_depth[slot][target];
     if(!depth||py+(int)depth<=(d->foot8*8)){unsigned bit=(ix-col)*bits,value=repeat?p[0]:p[bit>>3]>>(bit&7);if(!repeat&&(bit&7)+bits>8)value|=(unsigned)p[(bit>>3)+1]<<(8-(bit&7));if(palette[value&mask]){pixels[target]=palette[value&mask];if(d->frame>=FLOOR_ITEM_BASE)floor_item_pixels++;}}
     else floor_masked_pixels++;
    }
    p+=repeat?1:(run*bits+7)/8;col=end;
   }
  }
 }
}
/* EWRAM/ROM are word buses: merge four opaque/transparent pixels at a time. */
static __attribute__((noinline)) void terrain_piece(uint8_t *pixels,uint8_t *depth,const uint8_t *piece,unsigned z){
 uint32_t *out=(uint32_t*)pixels,*dst=(uint32_t*)depth;const uint32_t *src=(const uint32_t*)piece;unsigned zz=z*0x01010101u;
 for(unsigned p=0;p<16;p++){
  unsigned v=src[p];if(!v)continue;
  if(!((v-0x01010101u)&~v&0x80808080u)){out[p]=v;dst[p]=zz;continue;}
  unsigned mask=(((((v&0x7f7f7f7fu)+0x7f7f7f7fu)|v)&0x80808080u)>>7)*255u;
  out[p]=(out[p]&~mask)|v;dst[p]=(dst[p]&~mask)|(zz&mask);
 }
}
/* Fully populated checker tiles have a known alpha mask; no per-pixel
 * transparency discovery is needed while the camera exposes new terrain. */
static __attribute__((noinline)) void terrain_checker(uint8_t *pixels,uint8_t *depth,const uint8_t *piece,unsigned z){
 uint32_t *out=(uint32_t*)pixels,*dst=(uint32_t*)depth;const uint32_t *src=(const uint32_t*)piece;
 for(unsigned p=0;p<16;p++){
  unsigned mask=(p&2)?0xff00ff00u:0x00ff00ffu;
  out[p]=(out[p]&~mask)|src[p];dst[p]=(dst[p]&~mask)|(z*0x01010101u&mask);
 }
}
/* A wide prop can extend into a later cell's floor diamond. Ground has no
 * occluding depth and must fill around, rather than erase, that prop. This
 * rare path is used only after a prop tile was actually composed. */
static UI_FN void terrain_ground(uint8_t *pixels,const uint8_t *depth,const uint8_t *piece){
 for(unsigned p=0;p<64;p++)if(piece[p]&&!depth[p])pixels[p]=piece[p];
}
#include "terrain_fixed75.h"
/* Co-op floor_prepare deliberately skips corpses, leaving this entire arena
 * free. Keep raw terrain caches intact while staging final, software-lit
 * pixels. Do not enable co-op corpse caching without moving this loan. */
enum { COOP_TERRAIN_STAGE_TILES=240 };
#define COOP_TERRAIN_STAGE DEATH_CACHE
#define SOLO_TERRAIN_STAGE (DEATH_CACHE+3*4096)
#define COOP_TERRAIN_SLOTS ((uint16_t*)(DEATH_CACHE+COOP_TERRAIN_STAGE_TILES*64))
/* Corpses stop at OBJ slot10. The last4KiB may be borrowed only after
 * floor composition; invalidate floor-cache key5 before borrowing it. */
#define FRAME_MAP ((uint16_t*)(DEATH_SPRITE_STAGE+11*2048))
#define COOP_TERRAIN_INDEX (DEATH_SPRITE_STAGE+5*2048)
_Static_assert(COOP_TERRAIN_STAGE_TILES*66<=4*4096,"co-op terrain stage capacity");
static UI_FN void terrain_stage(unsigned slot,const void *pixels,unsigned invalid_raw){
 if(coop_active&&terrain_upload_count<COOP_TERRAIN_STAGE_TILES){
  dma(pixels,COOP_TERRAIN_STAGE+terrain_upload_count*64,64);
  COOP_TERRAIN_SLOTS[terrain_upload_count]=(uint16_t)slot;
 }else if(!coop_active&&invalid_raw&&!(SOLO_STAGE_USED[(slot&63)>>5]&(1u<<(slot&31)))){
  /* A collision falls back to the original safe staged upload. At most64
   * raw tiles survive here; no unbounded queue or extra EWRAM is required. */
  SOLO_STAGE_USED[(slot&63)>>5]|=1u<<(slot&31);
  dma(pixels,SOLO_TERRAIN_STAGE+(slot&63)*64,64);light_tiles[slot]|=128;
 }else{
  dma(pixels,TERRAIN_CACHE+slot*64,64);
  if(invalid_raw)light_tiles[slot]|=64;
 }
 light_tiles[slot]|=32;terrain_upload_count++;
}
#if PERF_TERRAIN_ROWS
static UI_FN int terrain_row_reusable(unsigned row,unsigned column,int tx,int y){
 unsigned bit=1u<<(row/31),dirty=terrain_dirty_rows&bit;
 terrain_dirty_rows&=~bit;
 if(dirty)return 0;
 unsigned first=row+column,last=row+(column?column-1:30);
 /* Completed rows own31 consecutive tiles. Interior invalidations mark the
  * row; endpoint equality then proves unchanged camera coverage. */
 return ring_x[first]==tx&&ring_y[first]==y&&ring_x[last]==tx+30&&ring_y[last]==y;
}
#endif
static void stream_dungeon(void){
    int tx=render_tile(cam_x),ty=render_tile(cam_y);
    const uint8_t *art=(const uint8_t*)town_map;
    const WorldArt *wa=dungeon_level>4?game_world_art(dungeon_level):0;
    const uint16_t *layout=(const uint16_t*)(art+DUNGEON_LAYOUT_OFFSET);
    const uint8_t *occludes=art+12800;
    unsigned height=wa?wa->height:80;
    const unsigned hardware=lighting_hardware_active();
    uint8_t *pixels=(uint8_t*)load_buffer;
    unsigned ring_row=render_row(ty),first_column=render_column(tx);
    for(int y=ty;y<=ty+20;y++){
#if PERF_TERRAIN_ROWS
     if((hardware||!light_moved)&&terrain_row_reusable(ring_row,first_column,tx,y))goto next_row;
#endif
     unsigned ring_column=first_column;
     for(int x=tx;x<=tx+30;x++){
        unsigned slot=ring_row+ring_column;
        if(++ring_column==31)ring_column=0;
        unsigned relocated=ring_x[slot]!=x||ring_y[slot]!=y;
        unsigned geometry=relocated;
        unsigned band=hardware?0:geometry||light_moved?lighting_tile(x*8,y*8):light_tiles[slot]&7;
        if(geometry || (light_tiles[slot]&15)!=band || (band==4&&light_moved)){
            /* A staged software-lit cache needs raw regeneration only when
             * its lighting/contents actually change, not on every idle draw. */
            geometry|=!!(light_tiles[slot]&64);
            unsigned composite=geometry?0:light_tiles[slot]&16;
            int px=x*8,py=y*8;
            if(geometry){
            death_sprite_invalidate_rect(px,py,8,8);
            for(int p=0;p<16;p++){((uint32_t*)pixels)[p]=0;((uint32_t*)town_depth[slot])[p]=0;}
            unsigned prop_composed=0;
            /* Ascending foot depth gives the same back-to-front piece order as L1. */
            int start=(py-72)/8;if(start<0)start=0;
            for(int sum=start;sum<=start+(int)(height/8+1) && sum<MAP_W+MAP_H-1;sum++){
                int foot=72+sum*8,dy=py-foot+(int)height;
                if(dy<0 || dy>=(int)height)continue;
                int across=px-(DUNGEON_ORIGIN-16)+sum*16,center=across>>5;
                /* A32px terrain piece covers only center; a64px prop also
                 * touches one neighbor. Retain their original depth order. */
                int first=center-((across&31)<16),last=first+1;
                /* The lossless BurnCross crop moves its64px image8px right.
                 * Only this one tile alignment needs the preceding anchor. */
                if((across&31)==16){
                    int cx=center-1,cy=sum-cx;
                    if(cx>=0&&cy>=0&&cx<MAP_W&&cy<MAP_H){
                        unsigned p=PROP_LOOKUP[cy*MAP_W+cx];
                        if(p&&game.props[p-1].kind==PROP_BURNING_CROSS)first--;
                    }
                }
                for(int cx=first;cx<=last;cx++){
                    int cy=sum-cx;if(cx<0 || cy<0 || cx>=MAP_W || cy>=MAP_H)continue;
                    int cell=cy*MAP_W+cx;
                    int dx=across-cx*32;
                    if(dx>=0&&dx<32){
                    unsigned stored=wa?0:layout[cell];
                    unsigned variant=wa?town_map[cell]:stored==DUNGEON_VISUAL_EMPTY?stored:stored&0x3fff;
                    if(wa||variant!=DUNGEON_VISUAL_EMPTY){
                    if(!wa)variant=dungeon_door_variant(&game,PROP_LOOKUP,variant,cx,cy);
                    unsigned extra=!wa&&variant>=DART_COUNT,row=wa&&wa->render_rows?wa->render_rows[variant]:(unsigned)variant,tile=(extra?variant-DART_COUNT:row)*(height/8*4)+(dy>>3)*4+(dx>>3),kind=wa?(wa->tiles[tile]&3):extra?floor_variant_flags[tile]:dart_tile_flags[tile];
                    if(kind){
                    const uint8_t *piece=wa?wa->art+(wa->tiles[tile]>>2)*64:extra?asset_floor_variants+floor_variant_tiles[tile]*64:asset_dungeon_art_raw+dart_tile_ids[tile]*64;
                    uint8_t z=(wa?(occludes[cell]&1):extra?0:dart_occludes[variant])?(uint8_t)(foot-py):0;
                    unsigned fixed_half=!wa&&!extra&&(stored&(dx<16?0x4000:0x8000))&&(!dart_door[stored&0x3fff]||dy<48||variant!=(stored&0x3fff));
                    if(fixed_half){unsigned id=dy<64?fixed_wall_upper[dx>=16][dart_tile_ids[tile]]:fixed_wall_lower[variant*8+((dy-64)>>3)*4+(dx>>3)];piece=fixed_wall_art+id*64;kind=fixed_wall_checker[id]?3:2;}
                    unsigned fixed_world=wa&&z&&(occludes[cell]&(dx<16?0x40:0x80))&&(!(occludes[cell]&0x20)||dy<(int)height-32);
                    if(fixed_world)terrain_fixed75(pixels,town_depth[slot],piece,z,dx,dy,height);
                    else if(!z&&prop_composed)terrain_ground(pixels,town_depth[slot],piece);
                    else if(kind==1){dma(piece,pixels,64);for(int p=0;p<16;p++)((uint32_t*)town_depth[slot])[p]=z*0x01010101u;}
                    else if(kind==3)terrain_checker(pixels,town_depth[slot],piece,z);
                    else terrain_piece(pixels,town_depth[slot],piece,z);
                    }
                    }
                    }
                    unsigned prop=PROP_LOOKUP[cell];
                    int prop_y=dy-(int)height+64,prop_x=dx+16;
                    if(prop&&game.props[prop-1].kind==PROP_BURNING_CROSS){prop_x-=8;prop_y+=8;}
                    if(prop && prop_y>=0 && prop_y<64 && prop_x>=0 && prop_x<64){
                        const uint8_t *pic=prop_tile(prop_draw_frames[prop-1],(unsigned)(prop_y>>3)*8+((unsigned)prop_x>>3));
                        if(pic){
                         prop_composed=1;
                         if(highlighted_prop==(int)prop-1){for(unsigned i=0;i<64;i++)if(pic[i]){pixels[i]=prop_highlight[pic[i]];town_depth[slot][i]=(uint8_t)(foot-py);}}
                         else terrain_piece(pixels,town_depth[slot],pic,(uint8_t)(foot-py));
                        }
                    }
                }
            }
            dma(pixels,TERRAIN_CACHE+slot*64,64);depth_max[slot]=0;
            unsigned any_depth=0;for(int p=0;p<16;p++)any_depth|=((uint32_t*)town_depth[slot])[p];
            if(any_depth)for(int p=0;p<64;p++)if(town_depth[slot][p] && y*8+town_depth[slot][p]>depth_max[slot])depth_max[slot]=(uint16_t)(y*8+town_depth[slot][p]);
            }else dma(TERRAIN_CACHE+slot*64,pixels,64);
            if(!composite){
                uint32_t bit=1u<<(x-tx);unsigned floor=FLOOR_COVERAGE[y-ty]&bit;
                if(floor)floor_overlay(px,py,slot,pixels);
                if(floor&&!(FLOOR_ANIMATED[y-ty]&bit)){
                    dma(pixels,TERRAIN_CACHE+slot*64,64);composite=16;
                }
            }
            if(band==4)lighting_pixels(pixels,px,py);else lighting_terrain(pixels,band);
            light_tiles[slot]=band|composite;
            /* Preserve the displayed frame, including lighting/decals at an
             * unchanged coordinate. Never update live terrain during draw. */
            terrain_stage(slot,pixels,band||((FLOOR_COVERAGE[y-ty]&(1u<<(x-tx)))&&!composite));
            /* Publish cache ownership only after its pending-upload flag. */
            if(geometry){ring_x[slot]=x;ring_y[slot]=y;}
        }
     }
#if PERF_TERRAIN_ROWS
next_row:
#endif
     ring_row+=31;if(ring_row==WORLD_TILES)ring_row=0;
    }
}
static int direction(int dx,int dy){
    int sx=dx-dy,sy=dx+dy;
    if(!sx)return sy>=0?0:4;
    if(!sy)return sx>0?6:2;
    return sy>0?(sx>0?7:1):(sx>0?5:3);
}
static UI_FN void place(Motion *m,int x,int y){
    m->x=m->sx=m->tx=((game.area?1536:DUNGEON_ORIGIN)+(x-y)*16)<<8;
    m->y=m->sy=m->ty=((game.area?128:72)+(x+y)*8)<<8;
    m->elapsed=m->total=0;m->dir=0;
}
static UI_FN void move_to(Motion *m,int x,int y,int oldx,int oldy,int duration){
    if(x==oldx&&y==oldy)return;
    if(x-oldx>1||oldx-x>1||y-oldy>1||oldy-y>1){place(m,x,y);return;}
    m->sx=m->x;m->sy=m->y;m->tx=((game.area?1536:DUNGEON_ORIGIN)+(x-y)*16)<<8;m->ty=((game.area?128:72)+(x+y)*8)<<8;
    m->elapsed=0;m->total=duration;m->dir=direction(x-oldx,y-oldy);
}
static void advance(Motion *m){
    if(m->elapsed<m->total){
        ++m->elapsed;
        m->x=m->sx+(m->tx-m->sx)*m->elapsed/m->total;
        m->y=m->sy+(m->ty-m->sy)*m->elapsed/m->total;
    }
}
static UI_FN void reset_visuals(void){
    item_appearance_reset();
    ++crowd_mask_epoch;
    for(unsigned i=0;i<4;i++)game_attack_repeat[i]=0;
    for(unsigned i=0;i<2;i++)item_sound_seen[i]=item_sound_events[i];
    game_arrows_reset();if(game_hero_class()==1||SPELL_CASTING(game.swing_target)){game.attack_wait=0;game.attack_queued=0;game.swing_target=-1;}
    map_last_x=-1;map_pending=0;map_init=1;
    game_ai_reset();game_finale_restore(&game);
    game_unique_restore(&game);
    place(&motion[0],game.x,game.y);
    for(int i=0;i<MAX_ENEMIES;i++)place(&motion[i+1],game.enemies[i].x,game.enemies[i].y);
    for(int i=0;i<13;i++){last_frame[i]=-1;last_asset[i]=0;last_dir[i]=-1;d_raw_base[i]=0;d_raw_ref[i]=255;}
    for(int i=0;i<4;i++)raw_base[i]=0;
    raw_next=0;actor_unmasked_slots=actor_clear_slots=actor_hidden_slots=0;
    last_hud_tick=0xffffffffu;
    automap_prime();
}
static void add_obj(int x,int y,int depth,int tile,int pal,int shape,int size){
    if(x<-64||x>=240||y<-80||y>=160)return;
    if(object_count<(game.area?40:84))objects[object_count++]=(Obj){(x&511)|(pal<<9)|(shape<<14),tile|(size<<14),y,depth};
}
/* Cached pixels are keyed by asset/direction/frame, never actor identity.
 * A reassigned OBJ slot only needs fresh masking when its world position changes. */
static UI_FN unsigned actor_clear_geometry(int wx,int wy,int foot){
    int left=wx>cam_x?wx:cam_x,top=wy>cam_y?wy:cam_y;
    int right=wx+64<cam_x+240?wx+64:cam_x+240,bottom=wy+64<cam_y+160?wy+64:cam_y+160;
    if(left>=right||top>=bottom)return 1;
    left=render_tile(left);right=render_tile(right-1);top=render_tile(top);bottom=render_tile(bottom-1);
    unsigned row=render_row(top),first=render_column(left);
    for(int y=top;y<=bottom;y++){
        unsigned col=first;
        for(int x=left;x<=right;x++){
            unsigned id=row+col;
            if(ring_x[id]!=x||ring_y[id]!=y||depth_max[id]>foot)return 0;
            if(++col==31)col=0;
        }
        row+=31;if(row==WORLD_TILES)row=0;
    }
    return 1;
}
/* An edge-clipped image remains valid when the new visible intersection is
 * contained in the old one. No previously unseen pixels may be exposed. */
static UI_FN unsigned actor_mask_window_valid(unsigned slot,int wx,int wy){
 if(mask_complete[slot]||(mask_cam_x[slot]==cam_x&&mask_cam_y[slot]==cam_y))return 1;
 int left=wx>cam_x?wx:cam_x,top=wy>cam_y?wy:cam_y;
 int right=wx+64<cam_x+240?wx+64:cam_x+240,bottom=wy+64<cam_y+160?wy+64:cam_y+160;
 return left>=right||top>=bottom||(left>=mask_cam_x[slot]&&right<=mask_cam_x[slot]+240&&top>=mask_cam_y[slot]&&bottom<=mask_cam_y[slot]+160);
}
static UI_FN unsigned actor_geometry_for_frame(unsigned slot,int wx,int wy,int foot){
    /* Cache the rectangle verdict, not whether the old pose lost any pixels.
     * A blocked verdict still takes the normal per-pixel path for a new pose.
     * Terrain edits invalidate last_frame; newly exposed edges need a recheck. */
    if(last_frame[slot]>=0&&mask_x[slot]==wx&&mask_y[slot]==wy&&actor_mask_foot[slot]==foot&&
       actor_mask_window_valid(slot,wx,wy))return (actor_clear_slots>>slot)&1;
    return actor_clear_geometry(wx,wy,foot);
}
static UI_FN void actor_record_geometry(unsigned slot,unsigned clear,unsigned hidden,int foot){
    unsigned bit=1u<<slot;
    if(!hidden&&!game.area)actor_unmasked_slots|=(uint16_t)bit;else actor_unmasked_slots&=(uint16_t)~bit;
    if(clear)actor_clear_slots|=(uint16_t)bit;else actor_clear_slots&=(uint16_t)~bit;
    actor_mask_foot[slot]=(int16_t)foot;
}
/* One or two world cells per row, then one packed-word AND. The hero still
 * counts removed opaque pixels exactly; monsters only need the changed bit. */
/* Compare four unsigned depth bytes in parallel. The high-bit split also
 * handles128px Hell pieces; transparent depth0 never covers an actor. */
static __attribute__((noinline)) unsigned actor_depth_row(const uint32_t *p,unsigned sub,unsigned high_half){
 static const uint16_t nibbles[16]={0,15,240,255,3840,3855,4080,4095,61440,61455,61680,61695,65280,65295,65520,65535};
 unsigned result=0;
 for(unsigned half=0;half<2;half++){
  unsigned v=p[half],high=v&0x80808080u,bits=((v|0x80808080u)-sub)&0x80808080u;
  bits=high_half?bits&high:bits|high;
  unsigned index=(((bits>>7)&0x01010101u)*0x01020408u)>>24;
  result|=(unsigned)nibbles[index]<<(half*16);
 }
 return result;
}
static inline __attribute__((always_inline)) const uint8_t *actor_depth_source(int tx,int ty,int foot){
 unsigned id=render_slot(tx,ty);
 return ring_x[id]==tx&&ring_y[id]==ty&&depth_max[id]>foot?town_depth[id]:0;
}
static __attribute__((noinline)) unsigned actor_mask_tile(uint32_t *words,int left,int top,int foot,unsigned hero,unsigned *hidden){
 /* Horizontal viewport bounds are shared by all eight rows of this tile. */
 unsigned any=0,shift=(left&7)*4;int tx=render_tile(left),cached_ty=RING_INVALID;
 const uint8_t *depth[2]={0,0};unsigned sub=0,high_half=0,visible=~0u;
 if(left<cam_x){unsigned clip=cam_x-left;visible=clip>=8?0:visible&(~0u<<(clip*4));}
 if(left+8>cam_x+240){int end=cam_x+240-left;visible=end<=0?0:visible&(~0u>>(32-end*4));}
 for(unsigned y=0;y<8;y++){
  unsigned before=words[y],hide=0;int sy=top+(int)y;
  if(before&&sy>=cam_y&&sy<cam_y+160){
   int ty=render_tile(sy);
   if(ty!=cached_ty){
    cached_ty=ty;int limit=foot-ty*8;if(limit<0)limit=0;
    sub=((limit&127)+1)*0x01010101u;high_half=limit>=128;
    for(unsigned part=0;part<2;part++){
     depth[part]=limit<255?actor_depth_source(tx+(int)part,ty,foot):0;
    }
   }
   unsigned row=(sy&7)*8;
   if(depth[0])hide=(PERF_COARSE_MASK&&!hero?~0u:actor_depth_row((const uint32_t*)(depth[0]+row),sub,high_half))>>shift;
   if(shift&&depth[1])hide|=(PERF_COARSE_MASK&&!hero?~0u:actor_depth_row((const uint32_t*)(depth[1]+row),sub,high_half))<<(32-shift);
   hide&=visible;
  }
  unsigned after=before&~hide,removed=before^after;
  if(removed){if(hero){removed|=removed>>1;removed|=removed>>2;*hidden+=((removed&0x11111111u)*0x11111111u)>>28;}else *hidden=1;}
  words[y]=after;any|=after;
 }
 return any;
}
/* Update new occupied tiles and clear tiles left by the old pose. Unowned or
 * invalidated slots may have held an 8bpp corpse/UI image: initialize all 2KiB.
 * Sparse runs always source zeroes for newly transparent tiles. */
static UI_FN void actor_upload_tiles(unsigned slot,const void *pixels,const uint32_t tiles[2],unsigned unknown){
    uint32_t changed[2]={tiles[0]|actor_vram_tiles[slot][0],tiles[1]|actor_vram_tiles[slot][1]};
    if(unknown)changed[0]=changed[1]=~0u;
    if(death_sprite_stage_actor(slot,pixels,changed)){
        /* Keep the displayed corpse intact until its replacement OAM commits. */
    }else if(unknown||(changed[0]==~0u&&changed[1]==~0u))dma(pixels,(void*)(0x06010000+slot*2048),2048);
    else{
        unsigned start=0;
        while(start<64){
            while(start<64&&!(changed[start>>5]&(1u<<(start&31))))start++;
            unsigned end=start;
            while(end<64&&(changed[end>>5]&(1u<<(end&31))))end++;
            if(end>start)dma((const uint8_t*)pixels+start*32,(void*)(0x06010000+slot*2048+start*32),(end-start)*32);
            start=end;
        }
    }
    actor_vram_tiles[slot][0]=tiles[0];actor_vram_tiles[slot][1]=tiles[1];
    if(tiles[0]|tiles[1])actor_hidden_slots&=(uint16_t)~(1u<<slot);else actor_hidden_slots|=(uint16_t)(1u<<slot);
}
static unsigned load_town_frame(int slot,const uint8_t *base,int dir,int frame,int count,int wx,int wy,int foot,unsigned bank){
    unsigned band=lighting_at(wx+32,foot-16);
    if(!game.area&&slot>0&&infravision_frame)band=actor_infra_tint?2:0;
    unsigned display_bank=band==2?5:actor_palette_resolve(bank,&band);
    if(light_actors[slot]==band && light_banks[slot]==bank && last_asset[slot]==base && last_dir[slot]==dir && last_frame[slot]==frame && mask_x[slot]==wx && mask_y[slot]==wy && actor_mask_foot[slot]==foot && actor_mask_window_valid(slot,wx,wy))return display_bank;
    unsigned clear=!game.area&&actor_geometry_for_frame(slot,wx,wy,foot),hidden=0;
    /* Position/camera changes need only OAM updates when the resident image has
     * no removed pixels and the newly visible geometry cannot cover it. */
    if(clear&&(actor_unmasked_slots&(1u<<slot))&&light_actors[slot]==band&&light_banks[slot]==bank&&last_asset[slot]==base&&last_dir[slot]==dir&&last_frame[slot]==frame)goto actor_ready;
    uint32_t tiles[2]={~0u,~0u};
    if(game.area)actor_frame_tiles(base,(unsigned)(dir*count+frame),tiles);
    unsigned unknown=last_frame[slot]<0||last_asset[slot]!=base;
    if(!game.area){
        /* Associative raw-frame cache: inactive OBJ slots retain recent walk
         * frames instead of leaving twelve buffers unused for a lone hero. */
        int cached=-1;
        d_raw_ref[slot]=255;
        for(int i=0;i<13;i++)if(d_raw_base[i]==base && d_raw_dir[i]==dir && d_raw_frame[i]==frame){cached=i;break;}
        if(cached<0){
            actor_frame_tiles(base,(unsigned)(dir*count+frame),tiles);
            /* Preserve the other visible actors' raw frames. Without these
             * references a full screen can turn one miss into thirteen. */
            unsigned pinned=0;for(int i=0;i<13;i++)if(d_raw_ref[i]<13)pinned|=1u<<d_raw_ref[i];
            while(pinned&(1u<<raw_next))if(++raw_next==13)raw_next=0;
            cached=raw_next;if(++raw_next==13)raw_next=0;
            uint8_t *dst=(uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET+MAP_W*MAP_H*2+cached*2048;
            d_raw_base[cached]=0;
            if(actor_frame_external(base,(unsigned)(dir*count+frame),dst,2048,load_buffer+ACTOR_SCRATCH_WORD,lz_word(base)==0x33525053u?asset_lightning_frame_pool:asset_hero_frame_pool,asset_hero_frame_pool)!=2048)return display_bank;
            D_RAW_TILES[cached][0]=tiles[0];D_RAW_TILES[cached][1]=tiles[1];
            d_raw_base[cached]=base;d_raw_dir[cached]=dir;d_raw_frame[cached]=frame;
        }
        tiles[0]=D_RAW_TILES[cached][0];tiles[1]=D_RAW_TILES[cached][1];
        d_raw_ref[slot]=(uint8_t)cached;
        const uint8_t *raw=(uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET+MAP_W*MAP_H*2+cached*2048;
        /* Bright actors on open ground need neither a scratch copy nor a
         * transparent-tile/depth pass. Keep the cached raw pixels untouched. */
        if(clear&&!band){actor_upload_tiles(slot,raw,tiles,unknown);goto actor_ready;}
        dma(raw,load_buffer,2048);
    }else{
    int cached=-1;
    for(int i=0;i<4;i++)if(raw_base[i]==base && raw_dir[i]==dir && raw_frame[i]==frame){cached=i;break;}
    if(cached>=0)dma(load_buffer+512*(cached+1),load_buffer,2048);
    else{
        if(actor_frame_external(base,(unsigned)(dir*count+frame),load_buffer,2048,load_buffer+ACTOR_SCRATCH_WORD,lz_word(base)==0x33525053u?asset_lightning_frame_pool:asset_hero_frame_pool,asset_hero_frame_pool)!=2048)return display_bank;
        cached=raw_next;raw_next=(raw_next+1)&3;
        dma(load_buffer,load_buffer+512*(cached+1),2048);
        raw_base[cached]=base;raw_dir[cached]=dir;raw_frame[cached]=frame;
    }
    }
    uint8_t *pixels=(uint8_t*)load_buffer;
    /* Reject whole 8x8 OBJ tiles before touching individual packed pixels. */
    if(!clear)for(unsigned tile=0;tile<64;tile++){
        /* Most 64x64 actor frames are transparent. Reject empty tiles before
         * any world/depth lookups, without changing the sprite pixels. */
        unsigned half=tile>>5,bit=1u<<(tile&31);if(!(tiles[half]&bit))continue;
        uint32_t *a=(uint32_t*)pixels+tile*8;
        if(!(a[0]|a[1]|a[2]|a[3]|a[4]|a[5]|a[6]|a[7])){tiles[half]&=~bit;continue;}
        int left=wx+(tile&7)*8,top=wy+(tile>>3)*8;
        if(!actor_mask_tile(a,left,top,foot,!slot,&hidden))tiles[half]&=~bit;
    }
    lighting_actor(pixels,band,bank,tiles);
    actor_upload_tiles(slot,load_buffer,tiles,unknown);
actor_ready:
    mask_x[slot]=wx;mask_y[slot]=wy;mask_cam_x[slot]=cam_x;mask_cam_y[slot]=cam_y;
    mask_complete[slot]=wx>=cam_x && wx+64<=cam_x+240 && wy>=cam_y && wy+64<=cam_y+160;
    if(!slot)hero_occluded_pixels=hidden;
    actor_record_geometry(slot,clear,hidden,foot);
    light_actors[slot]=band;light_banks[slot]=bank;
    last_asset[slot]=base;last_dir[slot]=dir;last_frame[slot]=frame;
    return display_bank;
}
#include "death_sprites.inc"
static volatile uint16_t *hud_map=(volatile uint16_t*)0x0600d800;
static UI_FN void hud_text_raw(int x,int y,const char *s){
    volatile uint16_t *map=hud_map;
    for(;*s && x<30;s++,x++){
        unsigned index=hud_glyph_index[(uint8_t)*s];index=index?index-1:36;
        map[y*32+x]=(uint16_t)font_tile((unsigned)index);
    }
}
static UI_FN void hud_text(int x,int y,const char *s){
    hud_font_color(HUD_WHITE);hud_text_raw(x,y,s);
}
static UI_FN void hud_text_color(int x,int y,const char *s,unsigned color){
    if(!hud_transparent&&hud_map==(volatile uint16_t*)0x0600d800){
        /* Paused menus have disabled BG2. Borrow41 of its dictionary's
         * 8bpp-sized cells, after the small font atC000..C23F. Gameplay
         * restoration invalidates/rebuilds the dictionary before showing it.
         * Keep normal menu glyphs intact so descriptions stay white/beige.
         * Inventory uses a staging map and supplies its own private font. */
        if(color>HUD_RED)color=HUD_WHITE;
        unsigned key=0x10000u|(color<<8);
        if(hud_font_ink!=key){hud_font_color_copy((void*)0x0600c240,color);hud_font_ink=key;}
        for(;*s&&x<30;s++,x++){
            unsigned index=hud_glyph_index[(uint8_t)*s];index=index?index-1:36;
            hud_map[y*32+x]=(uint16_t)(index==40?265:266+index);
        }
    }else{hud_font_color(color);hud_text_raw(x,y,s);}
}
static UI_FN void hud_title(int x,int y,const char *s){
    volatile uint16_t *map=hud_map;
    for(;*s&&x<30;s++,x++){
        unsigned index=0;while(glyphs[index]&&glyphs[index]!=*s)index++;
        if(!glyphs[index])index=36;
        map[y*32+x]=(uint16_t)large_font_tile(index);
    }
}
static UI_FN int nearby_npc(void){
    int best=-1,distance=10;
    for(int i=0;i<NPC_COUNT;i++){
        if(i==2&&!game_wounded_present())continue;
        int dx=game.x-npc_xy[i][0],dy=game.y-npc_xy[i][1],d=dx*dx+dy*dy;
        if(d<distance){best=i;distance=d;}
    }
    return best;
}
/* Two3x5 letters share an8px tile. The12-character WATER SUPPLY label
 * needs two more tiles, reserved at AAC0..AB3F in every automap path.
 * Rebuild both strips only when their values change. */
static UI_FN int overlay_ink(const char *text,unsigned len,int x,int y){
    if(x<1||y<1||y>5)return 0;
    unsigned col=(unsigned)(x-1)%4,index=(unsigned)(x-1)/4;
    if(col>=3||index>=len)return 0;
    const char *letters=OVERLAY_GLYPHS;unsigned n=0;
    while(letters[n]&&letters[n]!=text[index])n++;
    return letters[n]&&(overlay_rows[n][y-1]&(4u>>col));
}
static UI_FN __attribute__((target("thumb"),minsize)) void player_overlay(void){
    unsigned level=game.area?0:dungeon_level,key=level|((unsigned)game_difficulty<<8);
    if(overlay_level!=key){
        char line[13]="LEVEL: ";unsigned len=7;
        if(!level||level==REGION_KING||level==REGION_VILE||level==REGION_BONE||level==REGION_WATER){const char *town=level==REGION_WATER?"WATER SUPPLY":level==REGION_BONE?"BONE LAIR":level==REGION_VILE?"VILE LAIR":level?"KING LAIR":"TRISTRAM";len=level==REGION_WATER?12:level?9:8;for(unsigned i=0;i<len;i++)line[i]=town[i];}
        else{if(level>=10)line[len++]=(char)('0'+level/10);line[len++]=(char)('0'+level%10);}
        line[len]=0;
        uint8_t fg=overlay_fg,bg=overlay_bg;
        uint8_t *pixels=(uint8_t*)load_buffer;
        for(unsigned tile=0;tile<7;tile++)for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int px=(int)tile*8+x;
            pixels[tile*64+y*8+x]=overlay_ink(line,len,px,y)?fg:
                overlay_ink(line,len,px-1,y)||overlay_ink(line,len,px+1,y)||overlay_ink(line,len,px,y-1)||overlay_ink(line,len,px,y+1)?bg:0;
        }
        dma(pixels,(void*)0x0600b280,5*64);
        dma(pixels+5*64,(void*)0x0600aac0,2*64);
        char label[22]="DIFFICULTY: ";unsigned count=12;const char *name=game_difficulty_name(game_difficulty);
        for(unsigned i=0;name[i]&&count<21;i++)label[count++]=name[i];label[count]=0;
        for(unsigned tile=0;tile<11;tile++)for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int px=(int)tile*8+x;
            pixels[tile*64+y*8+x]=overlay_ink(label,count,px,y)?fg:
                overlay_ink(label,count,px-1,y)||overlay_ink(label,count,px+1,y)||overlay_ink(label,count,px,y-1)||overlay_ink(label,count,px,y+1)?bg:0;
        }
        dma(pixels,(void*)0x0600b3c0,8*64);dma(pixels+8*64,(void*)0x0600b7c0,3*64);overlay_level=key;
    }
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<23;x++){
        unsigned n=(y==0?0:5)+x;
        R16(0x0600d800+(y*32+x)*2)=(uint16_t)(x<(y==0?7:11)?(!y&&x>=5?171+x-5:n<13?202+n:223+n-13):128);
    }
}
#include "infopanel.inc"
static UI_FN void hud(void){
    if(last_hud_tick==game.ticks)return;last_hud_tick=game.ticks;
    player_overlay();info_world();
}
static void panel_map(void){
    for(int y=0;y<4;y++)for(int x=0;x<30;x++)hud_map[(y+16)*32+x]=(uint16_t)(x>=4&&x<26?128:129+y*30+x);
}
static UI_FN void vital_bars(void){
    unsigned choice=hero_progress.spell_slot[hero_progress.spell_active_slot],key=game_spell_icon_key(&game,choice);
    if(last_spell_icon!=key){
        /* Native textured atlas. A source transition updates four palette entries;
         * the 4bpp image is identical for scroll/staff/learned/disabled. */
        unsigned spell=key&255,type=key>>8;
        for(unsigned i=0;i<128;i++)load_buffer[i]=0;
        const uint8_t *source=(const uint8_t*)spell_flat_icons[spell];
        uint8_t *pixels=(uint8_t*)load_buffer;
        for(unsigned row=0;row<3;row++)for(unsigned i=0;i<96;i++){
            pixels[row*128+i]=source[row*96+i];
        }
        unsigned index=256+SPELL_FLAT_PALETTE_BANK*16+SPELL_FLAT_FIRST_COLOR;
        for(unsigned shade=0;shade<4;shade++,index++){
        unsigned color=spell_flat_colors[type?type-1:4][shade],adjusted=0;base_palette[index]=(uint16_t)color;
        for(unsigned shift=0;shift<15;shift+=5){unsigned v=((color>>shift)&31)*(8+gamma_level)/10;adjusted|=(v>31?31:v)<<shift;}
        R16(0x05000000+index*2)=(uint16_t)adjusted;
        }
        dma(load_buffer,(void*)(0x06010000+1008*32),512);last_spell_icon=key;
    }
    /* Visible24x24 at212,102: bottom125 mirrors CharBut at4,113..125. */
    oam[126*4]=102;oam[126*4+1]=212|0x8000;oam[126*4+2]=1008|(SPELL_FLAT_PALETTE_BANK<<12);
    /* CharBut frame2, above HP. Same unspent-stat rule as DrawLevelUpIcon. */
    if(hero_progress.points){oam[127*4]=113|0x4000;oam[127*4+1]=4|0x8000;oam[127*4+2]=1000|(5<<12);}
    if(last_hp!=game.hp || last_mana!=game.mana || last_max_mana!=game.max_mana)last_hud_tick=0xffffffffu;
    unsigned levels[2]={globe_level(game.hp,game_max_hp()),globe_level(game.mana,game.max_mana)};
    for(unsigned side=0;side<2;side++)if(globe_levels[side]!=levels[side]){
        globe_render((uint8_t*)load_buffer,side,levels[side],globe_colors);
        unsigned col=side?26:0;
        for(unsigned row=0;row<4;row++)dma((uint8_t*)load_buffer+row*256,(void*)(0x0600a000+(1+row*30+col)*64),256);
        globe_levels[side]=(uint8_t)levels[side];globe_uploads++;
    }
    if(last_belt==~0u){
        dma(belt_tiles,(void*)(0x06010000+BELT_TILE*32),sizeof(belt_tiles));
        dma(belt_right_tiles,(void*)(0x06010000+BELT_RIGHT_TILE*32),sizeof(belt_right_tiles));
        dma(belt_marker,(void*)(0x06010000+864*32),32);
    }
    last_hp=game.hp;last_mana=game.mana;last_max_mana=game.max_mana;last_belt=game.belt;
    for(int n=0;n<10;n++){int i=n+4;oam[i*4]=(uint16_t)(info_count?150-info_count*(hud_transparent?INFO_STEP:8):152);oam[i*4+1]=(uint16_t)(80+n*8);oam[i*4+2]=(uint16_t)(((n==9?BELT_RIGHT_TILE:BELT_TILE)+game_belt_code(game_belt_kind(&game,n)))|(5<<12));}
}
/* Keep the HUD calls together in ROM; draw() has a tight IWRAM budget. */
static UI_FN void gameplay_hud(void){hud();vital_bars();info_box_oam();durability_hud();}
#include "../generated/map_arrows.h"
/* Marker expansion belongs to scene loading. Doing it together with the
 * first full town-map upload can exceed one VBlank's transfer budget. */
static UI_FN void automap_load_markers(void){
    for(unsigned p=0;p<4;p++){
        uint32_t words[sizeof(map_arrows)/4];uint8_t *tiles=(uint8_t*)words;
        for(unsigned i=0;i<sizeof(map_arrows);i++){
            unsigned v=map_arrows[i];tiles[i]=(v&15?p+1:0)|(v&240?(p+1)<<4:0);
        }
        dma(tiles,(void*)(0x06010000+(MAP_ARROW_TILE+p*8)*32),sizeof(map_arrows));
    }
    static const uint32_t npc_dot[8]={0,0,0,0x00055000,0x00055000,0,0,0};
    dma(npc_dot,(void*)(0x06010000+MAP_NPC_TILE*32),32);
}
/* Sliding world-space512x256 minimap at200% geometry: E800..F7FF holds two BG2 screenblocks.
 * Skip the former64 dictionary cells atF000; BG3'sF800 mask stays owned
 * by outer_mask. All map scratch is transient between draw's tail and
 * the next early VBlank. Rebuilding explicitly invalidates town raw caches.
 * Pack compacts the1bpp raster into refs in place, then expands dictionary
 * bytes before VBlank. No actor/menu routine may reuse this pending payload. */
enum { MAP_TILES=2048, MAP_DICTIONARY=245, MAP_FALLBACK_DICTIONARY=309, MAP_WORLD_X=256, MAP_WORLD_Y=128 };
#define MAP_RASTER ((uint8_t*)load_buffer)
#define MAP_REFS ((uint16_t*)load_buffer)
#define MAP_DICTIONARY_BITS ((uint32_t*)((uint8_t*)load_buffer+((map_screen_fallback&1)?16160:16384)))
#define MAP_UPLOAD_TILES ((uint32_t*)((uint8_t*)load_buffer+4096))
#define MAP_UPLOAD_REFS ((uint16_t*)((uint8_t*)load_buffer+((map_screen_fallback&1)?14112:12064)))
_Static_assert(16384+MAP_DICTIONARY*8<=sizeof(load_buffer),"map dictionary scratch capacity");
_Static_assert(4096+MAP_DICTIONARY*32<=12064,"map expanded dictionary capacity");
_Static_assert(12064+4096<=16384,"map prepared refs overlap dictionary bits");
_Static_assert(16160+MAP_FALLBACK_DICTIONARY*8<=sizeof(load_buffer),"map fallback dictionary capacity");
_Static_assert(14112+2048<=16160,"map fallback prepared refs capacity");
_Static_assert(4096+MAP_FALLBACK_DICTIONARY*32<=14112,"map expanded fallback capacity");
/* BEA0..BFFF is after the mask's498..500 cells and before smallfontC000. */
/* The information background starts two pixels above its text. Clip only
 * the map there; terrain still continues beneath that translucent panel. */
static inline unsigned automap_bottom(void){return info_count?158-info_count*(hud_transparent?INFO_STEP:8):160;}
static void automap_blend(void){
    if(lighting_hardware_active()){
        /* BG2 is opaque and is not a brightness target. Priority1 information
         * OBJ panels cover it below the footer; there is no alpha second target. */
        R16(0x04000048)=0x1f1f;R16(0x0400004a)=0x003f;
        R16(0x04000050)=0x00c1;R16(0x04000054)=LIGHT_OUTSIDE_BLDY;return;
    }
    R16(0x04000040)=240;
    R16(0x04000044)=(uint16_t)(map_visible&&!map_init?automap_bottom():160);
    R16(0x04000048)=0x003f;
    R16(0x0400004a)=map_visible&&!map_init?0x003b:0x003a;
    R16(0x04000054)=0;
    R16(0x04000050)=0x3540; /* BG2 is never an alpha first target. */
    R16(0x04000052)=0x0a06; /* Retain the information-panel blend in this path. */
}
/* Map refresh is a cold upload; reserve fast RAM for the per-frame death path. */
static inline int map_round(int n,int divisor){return n<0?-((-n+divisor/2)/divisor):(n+divisor/2)/divisor;}
/* Dungeon vertices have an exact two-pixel step: no alternating narrow cells.
 * Town retains the scale of its precomputed map bitmap. */
static inline int map_vertex(int n){return game.area?map_round(n*16,9):n*2;}
static inline int map_point_x(int x,int y){return 2*(map_vertex(x)-map_vertex(y));}
static inline int map_point_y(int x,int y){return map_vertex(x)+map_vertex(y);}
static inline unsigned map_tile(unsigned n){return (map_screen_fallback&1)?(n<298?outer_map_tile(n):501+n-298):n<174?530+n:n<218?266+n-174:n<234?326+n-218:501+n-234;}
static UI_FN void automap_upload(void){
    unsigned n=map_unique_tiles,first=n<174?n:174;
    dma(MAP_UPLOAD_TILES,(void*)0x0600c240,first*32);
    if(map_screen_fallback&1){
        if(n>174){unsigned count=n-174;if(count>64)count=64;dma(MAP_UPLOAD_TILES+174*8,(void*)0x0600f000,count*32);}
        if(n>238){unsigned count=n-238;if(count>44)count=44;dma(MAP_UPLOAD_TILES+238*8,(void*)0x0600a140,count*32);}
        if(n>282){unsigned count=n-282;if(count>16)count=16;dma(MAP_UPLOAD_TILES+282*8,(void*)0x0600a8c0,count*32);}
        if(n>298)dma(MAP_UPLOAD_TILES+298*8,(void*)0x0600bea0,(n-298)*32);
    }else{
        if(n>174){unsigned count=n-174;if(count>44)count=44;dma(MAP_UPLOAD_TILES+174*8,(void*)0x0600a140,count*32);}
        if(n>218){unsigned count=n-218;if(count>16)count=16;dma(MAP_UPLOAD_TILES+218*8,(void*)0x0600a8c0,count*32);}
        if(n>234)dma(MAP_UPLOAD_TILES+234*8,(void*)0x0600bea0,(n-234)*32);
    }
    dma(MAP_UPLOAD_REFS,(void*)0x0600e800,(map_screen_fallback&1)?2048:4096);
}
static inline int map_origin_x(int x,int y){return MAP_WORLD_X-((map_point_x(x,y)+4)&~7);}
static inline int map_origin_y(int x,int y){return MAP_WORLD_Y-((map_point_y(x,y)+4)&~7);}
static UI_FN void automap_camera(void){
 map_ref_x=map_round((cam_x+120-(game.area?1536:DUNGEON_ORIGIN))*(game.area?2:1),game.area?9:4);
 map_ref_y=map_round((cam_y+84-(game.area?128:72))*(game.area?2:1),game.area?9:4);
 /* Snap true 2:1 diagonal travel as one vector, anchored at its destination.
  * Use subpixel motion directly so X/Y cannot cross rounding thresholds apart.
  * Pure-axis travel, interrupted non-diagonal segments and cinematic cameras
  * retain the normal projection. This adds no persistent scroll state. */
 if(!game.area){
  const Motion *m=&motion[0];int dx=m->tx-m->sx,dy=m->ty-m->sy;
  if(m->elapsed<m->total&&dy&&(dx==2*dy||dx==-2*dy)&&
     cam_x==(m->x>>8)-120&&cam_y==(m->y>>8)-84){
   int y=map_round(m->y-(72<<8),1024);
   int ty=map_round(m->ty-(72<<8),1024);
   int tx=map_round(m->tx-(DUNGEON_ORIGIN<<8),1024);
   map_ref_x=tx+(dx==2*dy?2:-2)*(y-ty);map_ref_y=y;
  }
 }
}
static UI_FN void automap_scroll(void){
 if(!map_visible||map_init)return;
 if(game.area||map_stream_active){R16(0x04000018)=(uint16_t)((map_ref_x-120)&255);R16(0x0400001a)=(uint16_t)((map_ref_y-80)&255);}
 else {R16(0x04000018)=(uint16_t)(map_origin_x(map_last_x,map_last_y)+map_ref_x-120);R16(0x0400001a)=(uint16_t)(map_origin_y(map_last_x,map_last_y)+map_ref_y-80);}
}
static UI_FN void automap_commit(void){
    if(death_red_ready)return; /* Death owns the shared map dictionary. */
    if(game.area||map_stream_active){
        automap_stream_commit();automap_scroll();
        if(map_visible&&!map_init)R16(0x04000000)|=0x0400;else R16(0x04000000)&=(uint16_t)~0x0400u;
        return;
    }
    if(!map_pending)return;
    if(!map_visible){map_pending=0;map_last_x=-1;map_last_revision=0xffffffffu;return;}
    unsigned before=R16(0x04000006);
    /* Popup/link work can finish late. Rebuild next frame rather than starting
     * a map upload after the safe VBlank window; scratch is reused by draw(). */
    if(before<160||before>170){map_pending=0;map_last_x=-1;map_last_revision=0xffffffffu;return;}
    map_init=0;
    if(map_pending==2)automap_upload();
    /* Regular 1:1 text tiles. Priority1 information OBJ backgrounds
     * win the equal-priority OBJ/BG tie, above the opaque map. */
    map_screen_live=map_screen_fallback;
    R16(0x0400000c)=OUTER_MAP_BG2_CONTROL|(map_screen_live&1?0:0x4000);
    automap_scroll();
    map_pending=0;map_commits++;
    if(before<160||R16(0x04000006)<160)map_commit_overruns++;
}
static UI_FN void automap_arrow(void){
    if(!map_visible||map_init)return;
    int bottom=(int)automap_bottom();
    if(coop_active){
     for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){
      int nx=coop_players[i].x,ny=coop_players[i].y;
      int x=120+map_point_x(nx,ny)-map_ref_x;
      int y=80+map_point_y(nx,ny)-map_ref_y;
      if(x<3||x>236||y<3||y>124||y+2>=bottom)continue;
      oam[i*4]=(uint16_t)(y-3);oam[i*4+1]=(uint16_t)(x-3);oam[i*4+2]=(uint16_t)((MAP_ARROW_TILE+i*8+(coop_players[i].facing&7))|(15<<12));
     }
    }else{oam[0]=77;oam[1]=117;oam[2]=(uint16_t)((MAP_ARROW_TILE+(game.facing&7))|(15<<12));}
    if(game.area)for(int i=0;i<NPC_COUNT;i++){
        if(i==2&&!game_wounded_present())continue;
        int nx=npc_xy[i][0],ny=npc_xy[i][1];
        int x=120+map_point_x(nx,ny)-map_ref_x;
        int y=80+map_point_y(nx,ny)-map_ref_y;
        if(x<1||x>=239||y<1||y>=127||y+1>=bottom)continue;
        int n=TOWN_MAP_NPC_OAM+i;
        oam[n*4]=(uint16_t)((y-3)&255);oam[n*4+1]=(uint16_t)((x-3)&511);
        oam[n*4+2]=(uint16_t)(996|(15<<12));
    }
}
#include "automap_raster.inc"
#include "automap_stream.inc"
#include "automap_cached.inc"
static UI_FN void automap_draw(void){
 if(!map_visible)return;
 /* Use bounded ring jobs for every dungeon. A moving wide-cache rebuild
  * previously stalled this draw for about 100ms on explored L1 saves. */
 if(map_last_x==-1)map_stream_active=1;
 if(game.area||map_stream_active)automap_stream_draw();else automap_cached_draw();
}
static UI_FN void invalidate_prop(int i){
 if(i<0||i>=MAX_PROPS)return;
 const Prop *p=&game.props[i];int x=DUNGEON_ORIGIN+(p->x-p->y)*16-32,y=72+(p->x+p->y)*8-64;
 if(p->kind==PROP_BURNING_CROSS){x+=8;y-=8;}
 int width=64,height=64;
 if(game_prop_is_door(p->kind)){x-=16;y-=24;width=96;height=88;}
 for(int slot=0;slot<WORLD_TILES;slot++)if(ring_x[slot]*8<x+width&&ring_x[slot]*8+8>x&&ring_y[slot]*8<y+height&&ring_y[slot]*8+8>y){terrain_invalidate(slot);ring_y[slot]=RING_INVALID;}
}
#include "prop_dirty.inc"
/* The Cleaver changes presentation only. Broken or unusable equipment does
 * not transform a hero; equipment resolution supplies sentinel27. */
static UI_FN unsigned hero_variant(const Inventory *inventory,const HeroProgress *stats,unsigned cls){
 return item_render_appearance(inventory,stats,cls);
}
static const HeroArt butcher_hero={asset_m71_6_n,asset_m71_6_w,asset_m71_6_a,asset_m71_6_h,asset_m71_6_n,asset_m71_6_w,10,8,12,6,10,8,7,0};
static UI_FN const HeroArt *hero_art(unsigned cls,unsigned variant){return variant==27?&butcher_hero:&hero_weapon_art[cls][variant];}
static UI_FN void hero_appearance_prepare(void){
 unsigned local=coop_active?coop_id:0;
 hero_render_variants[local]=hero_variant(coop_active?&coop_players[local].hero.inventory:&hero_inventory,&hero_progress,game_hero_class());
 if(coop_active){
  for(unsigned p=0;p<4;p++)if(p!=local&&(coop_members&(1u<<p)))hero_render_variants[p]=hero_variant(&coop_players[p].hero.inventory,&coop_players[p].hero.progress,coop_players[p].hero.reserved&255);
  for(unsigned p=0;p<4;p++)if(coop_players[p].hero.hp<=0&&hero_render_variants[p]==27)hero_render_variants[p]=0;
  return;
 }
 if(game.hp<=0&&hero_render_variants[local]==27)hero_render_variants[local]=0;
 unsigned form=hero_render_variants[0]==27;
 if(form==hero_palette_form)return;
 hero_palette_form=form;
 lz_decode(form?asset_m71_pal_6:hero_palette[game_hero_class()],load_buffer,sizeof(load_buffer));
 dma(load_buffer,(void*)0x05000200,32);if(!form)saturate_hero_palette(0);
 for(unsigned i=0;i<16;i++)base_palette[256+i]=R16(0x05000200+i*2);
 for(unsigned i=0;i<16;i++)light_obj[i]=light_color(base_palette+256,16,i,3);
 /* Corpse pixels reference all OBJ colors. Rebuild their lookup on the rare
  * equipment transition and invalidate images before changing bank0. */
 death_sprite_palette();
 ++crowd_mask_epoch;for(unsigned i=0;i<13;i++)last_frame[i]=-1;
 actor_palette_reset();apply_gamma();
}
#include "../generated/hero_death_pose.h"
typedef struct {const uint8_t *asset;int count,frame;} HeroPose;
typedef struct {uint16_t keys[EFFECT_TILE_SLOTS],tiles[EFFECT_TILE_SLOTS];unsigned count;} EffectPlan;
/* Plans retain the spell identity even when the atlas budget chooses its
 * independently source-reduced16px form. Never substitute another spell. */
static UI_FN unsigned spell_image(unsigned cls,unsigned frame){return (cls<<8)|((frame/3)%spell_effect_geometry[cls].frames+1);}
static UI_FN void effect_plan_add(EffectPlan *p,unsigned image){
 if(!image)return;for(unsigned i=0;i<p->count;i++)if(p->keys[i]==image)return;
 if(p->count<(game.area?10:EFFECT_TILE_SLOTS))p->keys[p->count++]=(uint16_t)image;
}
static UI_FN void effect_plan_spell(EffectPlan *p,unsigned image){
 unsigned cls=image>>8,n=spell_composite[cls][1],first=spell_composite[cls][0];
 if(n){unsigned missing=0;for(unsigned j=0;j<n;j++){unsigned key=((first+j)<<8)|(image&255),found=0;for(unsigned i=0;i<p->count;i++)found|=p->keys[i]==key;missing+=!found;}
  if(p->count+missing<=(game.area?10:EFFECT_TILE_SLOTS)){for(unsigned j=0;j<n;j++)effect_plan_add(p,((first+j)<<8)|(image&255));return;}}
 effect_plan_add(p,image);
}
static UI_FN unsigned effect_plan_tile(const EffectPlan *p,unsigned image){
 for(unsigned i=0;i<p->count;i++)if(p->keys[i]==image)return p->tiles[i];return EFFECT_TILE_NONE;
}
static UI_FN void effect_plan_commit(EffectPlan *p){
 uint8_t large[EFFECT_TILE_SLOTS]={0};unsigned blocks=p->count;
 for(unsigned i=0;i<p->count;i++)if((p->keys[i]&0xf000)!=0xf000&&spell_effect_geometry[p->keys[i]>>8].size==32){large[i]=1;blocks+=3;}
 /* Allocate large images first across both contiguous atlas regions.
  * Three fit in the old region and eight in the reclaimed actor region. */
 unsigned count=0;for(unsigned i=0;i<p->count;i++)count+=large[i];
 for(unsigned i=p->count;i&&(blocks>(game.area?10:EFFECT_TILE_SLOTS)||count>(game.area?2:11));){--i;if(large[i]){large[i]=0;blocks-=3;count--;}}
 uint8_t used[EFFECT_TILE_SLOTS]={0};
 unsigned first=game.area?6:0,limit=game.area?16:EFFECT_TILE_SLOTS;
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<p->count;i++)if(large[i]==(pass==0)){
  unsigned n=large[i]?4:1,slot;
  for(slot=first;slot+n<=limit;slot++){
   if(effect_tile_address(slot+n-1)!=effect_tile_address(slot)+(n-1)*4)continue;
   unsigned free=1;for(unsigned j=0;j<n;j++)if(used[slot+j])free=0;
   if(free)break;
  }
  if(slot+n>limit){p->tiles[i]=EFFECT_TILE_NONE;continue;}
  for(unsigned j=0;j<n;j++)used[slot+j]=1;
  p->tiles[i]=(uint16_t)slot;
 }
 for(unsigned i=0;i<p->count;i++){
  unsigned key=p->keys[i],big=large[i],slot=p->tiles[i];if(slot==EFFECT_TILE_NONE)continue;
  const uint32_t *pixels;
  if((key&0xf000)==0xf000)pixels=spell_arrow_tiles[key&15];
  else {unsigned cls=key>>8,frame=(key&255)-1;pixels=big?spell_large_tiles[spell_effect_geometry[cls].large+frame]:spell_small_tiles[spell_effect_geometry[cls].small+frame];}
  p->tiles[i]=(uint16_t)(effect_tiles_place(pixels,slot,big?4:1)|(big?0x4000:0));
 }
}

static UI_FN unsigned spell_art_class(unsigned spell,int vx,int vy){
 if(spell>36)return spell==MONSTER_MISSILE_ACID?6:spell==MONSTER_MISSILE_FLAME?4:0;
 if(spell!=SPELL_ELEMENTAL)return spell_effect_class[spell];
 int x=(vx>0)-(vx<0),y=(vy>0)-(vy<0);
 unsigned d=y>0?(x>0?0:x<0?2:1):y<0?(x>0?6:x<0?4:5):x<0?3:7;
 return 14+d;
}
static UI_FN void add_spell_obj(int wx,int wy,unsigned tile,unsigned cls){
 if(tile==EFFECT_TILE_NONE||cls>=sizeof(spell_effect_geometry)/sizeof(spell_effect_geometry[0]))return;
 unsigned size=tile&0x4000?32:16;
 int x=wx-cam_x+spell_effect_geometry[cls].center-(int)size/2,y=wy-cam_y+spell_effect_geometry[cls].bottom-(int)size;
 add_obj(x,y,wy,tile&1023,spell_effect_geometry[cls].bank,0,size==32?2:1);
}
static UI_FN void draw_spell_image(const EffectPlan *p,int wx,int wy,unsigned key){
 unsigned tile=effect_plan_tile(p,key);if(tile!=EFFECT_TILE_NONE){add_spell_obj(wx,wy,tile,key>>8);return;}
 unsigned cls=key>>8,first=spell_composite[cls][0],count=spell_composite[cls][1];
 for(unsigned j=0;j<count;j++)add_spell_obj(wx,wy,effect_plan_tile(p,((first+j)<<8)|(key&255)),first+j);
}
static UI_FN unsigned persistent_image(unsigned index,unsigned spell,unsigned frame){
 unsigned growth=spell==SPELL_FIREWALL?game_spell_firewall_stage(index):0;
 if(growth)return (22<<8)|growth;
 unsigned native_frame=0,cls=game_spell_effect_pose(index,frame,&native_frame);return cls?(cls<<8)|(native_frame+1):spell_image(spell_effect_class[spell],frame);
}
static UI_FN void draw_spell_effects(unsigned frame){
 uint16_t arrows[MAX_ARROWS]={0},bolts[MAX_SPELL_BOLTS]={0};uint8_t effects[SPELL_EFFECT_VIEWS]={0};
 unsigned portal=spell_image(10,frame),shield=spell_image(11,0),phase=coop_active||game.paused?0:projectile_phase;
 EffectPlan plan={0};int x,y;
 unsigned red=0;int redx=0,redy=0;
 if(!game.area&&((dungeon_level==15&&quest_progress.stage[15]>=2)||(dungeon_level==REGION_VILE&&quest_progress.state[15]==3))){
  for(unsigned i=0;i<MAX_PROPS;i++)if(game.props[i].kind==PROP_VILE_ENTRY){red=spell_image(220,frame);redx=DUNGEON_ORIGIN+(game.props[i].x-game.props[i].y)*16;redy=72+(game.props[i].x+game.props[i].y)*8;break;}
 }
 if(red&&redx>=cam_x-32&&redx<=cam_x+272&&redy>=cam_y-16&&redy<=cam_y+208)effect_plan_spell(&plan,red);else red=0;
 /* Ensure persistent portal/shield pixels have their own identity. */
 for(unsigned owner=0;owner<4;owner++)if(game_spell_portal_at(&game,owner,&x,&y))effect_plan_spell(&plan,portal);
 unsigned shielded=game_spell_shielded(coop_active?coop_id:0)&&game.hp>0;if(shielded)effect_plan_add(&plan,shield);
 if(coop_active)for(unsigned p=0;p<4;p++)if((coop_members&(1u<<p))&&game_spell_shielded(p))effect_plan_add(&plan,shield);
 if(!game.area){
  for(unsigned i=0;i<MAX_ARROWS;i++)if(game_arrows[i].life){
   const Arrow *a=&game_arrows[i];if(a->owner==HOSTILE_TRAP_OWNER&&a->reserved==GAME_TRAP_LIGHTNING&&!(a->direction&HOSTILE_BURST))continue;
   unsigned spell=(a->direction&64)?a->fire:0;
   unsigned visual=spell?spell:((a->direction&(HOSTILE_PROJECTILE|HOSTILE_FARROW))==(HOSTILE_PROJECTILE|HOSTILE_FARROW)?SPELL_FIREBOLT:a->fire?1:a->lightning?3:0);
   int wx,wy;projectile_world(a,spell,&wx,&wy);if(wx<cam_x-32||wx>cam_x+272||wy<cam_y-16||wy>cam_y+208)continue;
   unsigned cls=visual==MONSTER_MISSILE_ACID?((a->direction&HOSTILE_BURST)?48:23+(a->direction&15)):visual==MONSTER_MISSILE_MAGMA?39+((a->direction&15)/2):spell_art_class(visual,a->vx,a->vy);
   arrows[i]=visual?spell_image(cls,frame):0xf000|(a->direction&15);effect_plan_add(&plan,arrows[i]);
  }
  if(game_spell_bolts_active())for(unsigned i=0;i<MAX_SPELL_BOLTS;i++){
   int wx,wy;unsigned spell=game_spell_bolt_world(i,projectile_phase,&wx,&wy);
   if(!spell||wx<cam_x-32||wx>cam_x+272||wy<cam_y-16||wy>cam_y+208)continue;
   bolts[i]=spell_image(spell_art_class(spell,game_spell_bolts[i].vx,game_spell_bolts[i].vy),frame);effect_plan_add(&plan,bolts[i]);
  }
  if(game_spell_effects_active())for(unsigned i=0;i<SPELL_EFFECT_VIEWS;i++){
   int wx,wy;unsigned spell=game_spell_effect_view_phase(i,phase,&wx,&wy);
   if(!spell||wx<cam_x-32||wx>cam_x+272||wy<cam_y-16||wy>cam_y+208)continue;
   effects[i]=(uint8_t)spell;effect_plan_spell(&plan,persistent_image(i,spell,frame));
  }
 }
 effect_plan_commit(&plan);
 for(unsigned i=0;i<MAX_ARROWS;i++)if(arrows[i]){
  const Arrow *a=&game_arrows[i];unsigned spell=(a->direction&64)?a->fire:0,tile=effect_plan_tile(&plan,arrows[i]);
  int wx,wy;projectile_world(a,spell,&wx,&wy);
  if((arrows[i]&0xf000)!=0xf000)add_spell_obj(wx,wy,tile,arrows[i]>>8);
  else if(tile!=EFFECT_TILE_NONE)add_obj(wx-cam_x-8,wy-cam_y-32,wy,tile,3,0,1);
 }
 for(unsigned i=0;i<MAX_SPELL_BOLTS;i++)if(bolts[i]){
  int wx,wy;unsigned spell=game_spell_bolt_world(i,projectile_phase,&wx,&wy);unsigned tile=effect_plan_tile(&plan,bolts[i]);
  if(spell==SPELL_LIGHTNING||spell==SPELL_CHAIN){const SpellBolt *a=&game_spell_bolts[i];int dx=(a->vx-a->vy)*16,dy=(a->vx+a->vy)*8;int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy,norm=ax>ay?ax:ay;if(!norm)norm=1;
   const SpellCast *cast=&game_spell_casts[a->cast-1];unsigned age=50; /* limited by traveled distance from the cast */
   age=(spell==SPELL_CHAIN?50:100)-cast->ticks;unsigned count=coop_active?1:age<4?age+1:4;
   for(unsigned n=0;n<count;n++)add_spell_obj(wx-dx*(int)(n*10)/norm,wy-dy*(int)(n*10)/norm,tile,bolts[i]>>8);
  }else add_spell_obj(wx,wy,tile,bolts[i]>>8);
 }
 for(unsigned i=0;i<SPELL_EFFECT_VIEWS;i++)if(effects[i]){
  int wx,wy;unsigned spell=game_spell_effect_view_phase(i,phase,&wx,&wy),key=persistent_image(i,spell,frame);
  draw_spell_image(&plan,wx,wy,key);
 }
 for(unsigned owner=0;owner<4;owner++)if(game_spell_portal_at(&game,owner,&x,&y))draw_spell_image(&plan,(game.area?1536:DUNGEON_ORIGIN)+(x-y)*16,(game.area?128:72)+(x+y)*8,portal);
 if(red)draw_spell_image(&plan,redx,redy,red);
 if(shielded)add_spell_obj(motion[0].x>>8,motion[0].y>>8,effect_plan_tile(&plan,shield),11);
 if(coop_active)for(unsigned p=0;p<4;p++)if(p!=coop_id&&(coop_members&(1u<<p))&&coop_players[p].hero.hp>0&&game_spell_shielded(p))add_spell_obj(coop_motion[p].x>>8,coop_motion[p].y>>8,effect_plan_tile(&plan,shield),11);
}
/* Two idle keyframes in multiplayer. Quantize the original phase rather
 * than changing its clock, so the full idle cycle keeps its original duration. */
static UI_FN unsigned idle_pose_frame(unsigned clock,unsigned count){
 if(!count)return 0;
 unsigned phase=(clock/9)%count;
 return coop_active?(phase*2/count)*(count/2):phase;
}
#include "attack_pose.h"
static UI_FN void hero_pose(const HeroArt *hero,unsigned repeat,int hp,const Motion *m,unsigned clock,unsigned moving,unsigned attack,unsigned duration,unsigned hit,int swing,unsigned cls,unsigned variant,HeroPose *pose){
 if(hp<=0){pose->asset=hero_death_pose[cls>2?0:cls][variant<27?variant/9:0];pose->count=1;pose->frame=0;return;}
 if((hit&255)&&!game.area){
  unsigned remaining=hit&255,total=(hit>>8)&255;int block=((hit>>16)&1)&&variant!=27;
  pose->asset=block&&block_hero_art[cls][variant].data?block_hero_art[cls][variant].data:hero->hit;
  pose->count=block&&block_hero_art[cls][variant].data?block_hero_art[cls][variant].frames:hero->hit_count;
  if(!total)total=pose->count;pose->frame=remaining==1?pose->count-1:remaining<total?(int)((total-remaining)*pose->count/total):0;
 }
 else if(variant!=27&&attack&&SPELL_CASTING(swing)&&(!game.area||swing==SPELL_SKILL_SWING)){
  unsigned id=SPELL_FROM_SWING(swing),family=id<=36?spell_cast_family[id]:1;
  pose->asset=family==2?lightning_hero_art[cls][variant].data:spell_hero_art[cls][variant][family].data;
  pose->count=family==2?lightning_hero_art[cls][variant].frames:spell_hero_art[cls][variant][family].frames;
  unsigned elapsed=game_spell_frames(cls)-attack,release=game_spell_release(cls)-1;
  pose->frame=family==1?(elapsed<=release?elapsed*2/release:2+(elapsed-release)*(pose->count-3)/(game_spell_frames(cls)-1-release)):
                    family==2?lightning_hero_cast_frame[cls][elapsed<20?elapsed:19]:spell_hero_cast_frame[cls][elapsed<20?elapsed:19];
 }else if(attack&&(!game.area||swing==SPELL_SKILL_SWING)){
  pose->asset=hero->attack;pose->count=hero->attack_count;
  unsigned impact_wait=game_attack_hit_wait(cls,duration);
  if(variant==27&&SPELL_CASTING(swing))duration=game_spell_frames(cls);
  else duration=game_attack_ticks(duration);
  if(!duration)duration=hero->bow?BOW_ATTACK_TICKS:ATTACK_TICKS;
  unsigned elapsed=attack<duration?duration-attack:0;
  unsigned impact_tick=variant==27&&SPELL_CASTING(swing)?game_spell_release(cls)-1:duration>impact_wait?duration-impact_wait:1;
  /* Use the pinned speed and show the original AFNum on the actual
   * impact/release tick, including barrels and the Butcher appearance. */
  pose->frame=(int)attack_pose_frame(hero->attack_count,hero->impact,duration,impact_tick,elapsed,repeat&&!SPELL_CASTING(swing));
 }else if(moving||m->elapsed<m->total){
  pose->asset=game.area?hero->town_walk:hero->walk;pose->count=game.area?hero->town_walk_count:hero->walk_count;
  pose->frame=(m->elapsed>0?m->elapsed-1:0)*pose->count/(m->total>0?m->total:1);
 }else{
  pose->asset=game.area?hero->town_idle:hero->idle;pose->count=game.area?hero->town_idle_count:hero->idle_count;
  pose->frame=(int)idle_pose_frame(clock,(unsigned)pose->count);
 }
 pose->frame=clamp(pose->frame,0,pose->count-1);
}
static UI_FN void coop_draw_remote(unsigned frame,int *sprite_slot){
 if(!coop_active)return;
 for(unsigned i=0;i<4;i++)if(i!=coop_id&&(coop_members&(1u<<i))){
  const CoopPlayer *p=&coop_players[i];Motion *m=&coop_motion[i];
  int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;if(x<-32||x>272||y<0||y>208)continue;
  unsigned cls=p->hero.reserved&255;if(cls>2)cls=0;
  unsigned variant=hero_render_variants[i];
  const HeroArt *hero=hero_art(cls,variant);HeroPose pose;
  hero_pose(hero,game_attack_repeat[i],p->hero.hp,m,frame,p->move_wait,p->attack_wait,p->attack_duration,p->hit_flash|(p->reaction_duration<<8)|(p->reaction_block<<16),p->swing,cls,variant,&pose);
  if(*sprite_slot>=13)continue;int slot=(*sprite_slot)++;
  unsigned bank=hero_palette_bank(variant==27?3:cls);
  int lift=variant==27?63:60;
  bank=load_town_frame(slot,pose.asset,p->facing,pose.frame,pose.count,(m->x>>8)-32,(m->y>>8)-lift,m->y>>8,bank);
  add_obj(x-32,y-lift,m->y>>8,slot*64,(int)bank,0,3);
 }
}
/* Visible town actors share the thirteen sprite caches. NPCs start after all
 * local/remote heroes, so neither their uploads nor their OBJ tiles alias. */
static UI_FN void draw_town_npcs(unsigned frame,int *sprite_slot){
 if(!game.area)return;
 for(int i=0;i<NPC_COUNT;i++){
  if(i==2&&!game_wounded_present())continue;
  int wx=1536+(npc_xy[i][0]-npc_xy[i][1])*16,wy=128+(npc_xy[i][0]+npc_xy[i][1])*8;
  int x=wx-cam_x,y=wy-cam_y;
  if(x<-32||x>272||y<0||y>208)continue;
  if(*sprite_slot>=13)break;
  int slot=(*sprite_slot)++;
  load_town_frame(slot,npc_assets[i],i>=9?i-9:0,i==2&&butcher_quest==2?0:(int)(frame/9)%npc_frames[i],npc_frames[i],wx-32,wy-63,wy,i>=9?6:6+i);
  add_obj(x-32,y-63,wy,slot*64,i>=9?6:6+i,0,3);
 }
}
static UI_FN void camera_position(void){
 int x=motion[0].x>>8,y=motion[0].y>>8;
 if(!game.area&&dungeon_level==16&&quest_progress.state[5]==3){
  unsigned phase=quest_progress.stage[5];if(phase>20)phase=20;
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].kind==MONSTER_DIABLO){const Enemy *e=&game.enemies[i];int tx=DUNGEON_ORIGIN+(e->x-e->y)*16,ty=72+(e->x+e->y)*8;x+=(tx-x)*(int)phase/20;y+=(ty-y)*(int)phase/20;break;}
 }
 cam_x=x-120;cam_y=y-84;
}
/* Prepare the initial ring while entering a dungeon, before live input and
 * simulation resume. Normal movement only queues the entering strips. */
static UI_FN void automap_prime(void){
 if(game.area)return;
 unsigned visible=map_visible;map_visible=1;map_stream_active=1;
 camera_position();automap_camera();game_explore_update(&game);
 for(unsigned jobs=0;jobs<128;jobs++){
  automap_stream_draw();automap_stream_commit();
  if(!map_init||MS->ready==2)break;
 }
 map_visible=visible;
}

static UI_FN unsigned outer_mask_gameplay(void){
 return outer_mask_present(0); /* Hardware dimming only; no opaque/dither overlay. */
}
static unsigned last_bow_shots;
/* Special selection lives in ROM; the normal per-frame draw loop keeps its
 * small IWRAM budget. Only active special poses call this adapter. */
static UI_FN void monster_special_pose(unsigned i,Motion *m,const uint8_t **data,int *frames,int *frame){
 unsigned kind=game_monster_base_kind(game.enemies[i].kind),ticks=game_enemy_special(&game,i);const uint8_t *base;int count,anim;
 if(monster_special_art[kind].data){const HitArt *a=&monster_special_art[kind];*data=a->data;*frames=a->count;unsigned t=game_monster_ai(kind)==MONSTER_AI_SNEAK||game_monster_ai(kind)==MONSTER_AI_COUNSLR?game_enemy_fade_frame(i):a->count>ticks?a->count-ticks:0;*frame=t<a->count?t:a->count-1;m->dir=game.enemy_facing[i];return;}
 if(game_enemy_spawning(&game,i)){base=kind==10?asset_skeleton_spawn_axe:kind==13?asset_skeleton_spawn_sword:asset_skeleton_spawn_bow;count=SKELETON_SPAWN_FRAMES;anim=count-ticks;}
 else if(game_enemy_charging(&game,i)){base=kind>=MONSTER_FIEND?monster_art[kind].w:asset_ai_rhino_s;count=kind>=MONSTER_FIEND?monster_art[kind].walk:6;anim=count-ticks;}
 else if(kind==28||kind==MONSTER_UNSEEN){base=asset_ai_sneak_s;count=15;anim=game_enemy_fade_frame(i);}
 else if((kind>=15&&kind<=18)||kind==MONSTER_GHARBAD||kind==MONSTER_OVERLORD){
  int fat=kind==MONSTER_OVERLORD;base=fat?asset_ai_fat_s:asset_ai_goat_s;count=fat?10:12;anim=count-ticks;
 }else{
  int king=kind==MONSTER_KING,spear=kind==2||kind==MONSTER_DEVIL_SPEAR||kind==MONSTER_BLUE_SPEAR||kind==MONSTER_SNOTSPIL;
  base=king?asset_skeleton_king_s:spear?asset_ai_fallen_spear_s:asset_carver_sword_s;
  if(kind>=MONSTER_ADVOCATE)base=kind==MONSTER_DIABLO?asset_diablo_s:kind==MONSTER_ZHAR?asset_zhar_s:kind==MONSTER_LAZARUS?asset_lazarus_s:asset_advocate_s;
  count=kind>=MONSTER_ADVOCATE?(kind==MONSTER_DIABLO?16:20):king?6:spear?FRAMES_FALLEN_SPEAR_S:FRAMES_FALLEN_SWORD_S;
  anim=(kind==MONSTER_ADVOCATE||kind==MONSTER_LAZARUS||kind==MONSTER_ZHAR)?(int)game_enemy_fade_frame(i):king?(18-(int)ticks)/3:count-(int)ticks;
 }
 if(anim<0)anim=0;if(anim>=count)anim=count-1;
 *data=base;*frames=count;*frame=anim;m->dir=game.enemy_facing[i];
}
/* Keep a folded/waking gargoyle in its frozen Special pose during Stone
 * Curse. Other monsters retain the existing port's stone idle selection. */
static UI_FN void monster_stone_pose(unsigned i,Motion *m,const uint8_t **data,int *frames,int *frame){
 if(game_monster_ai(game.enemies[i].kind)==MONSTER_AI_GARG&&game_enemy_special(&game,i)){
  monster_special_pose(i,m,data,frames,frame);return;
 }
 const MonsterArt *a=&monster_art[game.enemies[i].kind];*data=a->n;*frames=a->idle;*frame=0;
}
static int crowd_dormant_actor=-1;
static UI_FN unsigned dormant_image_clear(unsigned slot,int wx,int wy,int foot);
#define CROWD_IMAGE_CLEAR(slot,wx,wy,foot) (crowd_dormant_actor<0?crowd_image_clear(slot,wx,wy,foot):dormant_image_clear(slot,wx,wy,foot))
#include "crowd_frames.inc"
/* Static props keep their decoded frame until their four-byte state changes.
 * Time/quest/player-dependent kinds always use the authoritative frame path. */
static UI_FN void prop_frames_prepare(void){
 static const uint8_t dynamic[256]={
  [PROP_FLAME_X]=1,[PROP_FLAME_Y]=1,[PROP_BLOOD_FOUNTAIN]=1,[PROP_PURIFYING_FOUNTAIN]=1,
  [PROP_MURKY_FOUNTAIN]=1,[PROP_TEAR_FOUNTAIN]=1,[PROP_THEME_CANDLE]=1,[PROP_SKEL_FIRE]=1,
  [PROP_BURNING_CROSS]=1,[PROP_GOAT_SHRINE]=1,[PROP_BRAZIER]=1,[PROP_WATER_CANDLE]=1,
  [PROP_VILE_CIRCLE]=1,[PROP_STORY_BOOK]=1,[PROP_VALOR_PEDESTAL]=1
 };
 for(unsigned i=0;i<MAX_PROPS;i++)if(game.props[i].kind){
  const Prop *p=&game.props[i];unsigned key=p->x|((unsigned)p->y<<8)|((unsigned)p->kind<<16)|((unsigned)p->state<<24);
  if(prop_draw_frames[i]!=255&&D_PROP_KEYS[i]==key&&!dynamic[p->kind])continue;
  if(game_prop_is_door(p->kind)&&D_PROP_KEYS[i]!=key)closed_door_cull_valid=0;
  #if PERF_PROP_STAGGER || PERF_OFFSCREEN_PROPS
  /* Only decorative animation may wait. A state change is immediate.
   * Include the full 64px sprite, the cross offset and a tile of margin;
   * catch up to the authoritative clock before entering the streamed view. */
  unsigned decorative=game_prop_is_fountain(p->kind)||p->kind==PROP_THEME_CANDLE||p->kind==PROP_WATER_CANDLE||p->kind==PROP_SKEL_FIRE||p->kind==PROP_GOAT_SHRINE||p->kind==PROP_BRAZIER||p->kind==PROP_BURNING_CROSS;
  if(decorative&&prop_draw_frames[i]!=255&&D_PROP_KEYS[i]==key){
#if PERF_OFFSCREEN_PROPS
   int px=DUNGEON_ORIGIN+((int)p->x-p->y)*16-32,py=72+(p->x+p->y)*8-64;
   if(p->kind==PROP_BURNING_CROSS){px+=8;py-=8;}
   if(px+64<cam_x-8||px>cam_x+248||py+64<cam_y-8||py>cam_y+168)continue;
#endif
#if PERF_PROP_STAGGER
   if((graphics_frames+i)&3u)continue;
#endif
  }
#endif
  unsigned f=game_prop_frame(&game,i);D_PROP_KEYS[i]=key;
  if(prop_draw_frames[i]!=f){prop_frame_changed(i,prop_draw_frames[i],f);prop_draw_frames[i]=(uint8_t)f;}
 }
}
#include "closed_door_monsters.inc"
#include "door_graph.inc"
#include "dormant_monsters.inc"
/* Calculate the upload budget BEFORE waiting for VBlank. Counting actor
 * runs after scanline160 consumed the safe window and forced an extra wait. */
static UI_FN void frame_prepare_commit(void){
 unsigned last=death_sprite_upload_pending()?death_sprite_commit_last_line():210u;
 unsigned n=terrain_upload_count;
 unsigned lines=12+(n?8+(n+3)/4:0)+effect_tiles_base_budget();
 frame_oversized=(uint16_t)(last<160+lines);
 frame_deadline=(uint16_t)(frame_oversized?160:last-lines);
 if(!game.area&&n){
  FLOOR_EXTRA_KEYS[5]=65535;
  dma((const void*)0x0600e000,FRAME_MAP,2048);
  for(unsigned i=0;i<WORLD_TILES;i++)if(light_tiles[i]&32)
   FRAME_MAP[((ring_y[i]&31)*32)+(ring_x[i]&31)]=(uint16_t)world_tile(i);
  if(coop_active){
   for(unsigned i=0;i<WORLD_TILES;i++)COOP_TERRAIN_INDEX[i]=0;
   for(unsigned i=0;i<n&&i<COOP_TERRAIN_STAGE_TILES;i++)COOP_TERRAIN_INDEX[COOP_TERRAIN_SLOTS[i]]=(uint8_t)(i+1);
  }
 }
}
static void draw(unsigned frame){
    unsigned profile_start=audio_game_clock();
    terrain_upload_count=0;
    if(!game.area)SOLO_STAGE_USED[0]=SOLO_STAGE_USED[1]=0;
    hud_gameplay();
    item_sound_update();
    if(hazard_audio_seen!=hazard_audio_event){hazard_audio_seen=hazard_audio_event;audio_hazard(hazard_audio_seen&255);}
    if(bow_shots!=last_bow_shots){if(bow_shots>last_bow_shots)audio_effect(25,1);last_bow_shots=bow_shots;}
    hero_appearance_prepare();
    camera_position();
    unsigned old_hardware=lighting_hardware_active();
    lighting_sources();lighting_hardware_prepare();
    if(old_hardware!=lighting_hardware_active()){
        light_moved=1;for(unsigned i=0;i<WORLD_TILES;i++){light_tiles[i]|=8;terrain_dirty(i);}
    }
    if(game.area)stream_town();else {
        int selected=game_prop_target(&game);if(selected!=highlighted_prop){invalidate_prop(highlighted_prop);invalidate_prop(selected);highlighted_prop=selected;}
        prop_frames_prepare();
        floor_prepare();stream_dungeon();
    }
    automap_camera(); /* Hardware camera moves with the matching actor OAM. */
    infravision_frame=!!game_infravision();
    closed_door_prepare();closed_door_skipped=0;
    dormant_motion_prepare();
    actor_palette_plan();
    object_count=0;int sprite_slot=0;
    for(int i=0;i<MAX_ENEMIES+1;i++){
        if(i==1)coop_draw_remote(frame,&sprite_slot);
        /* Keep room for projectiles/HUD and bound 64px OBJ scanline work.
         * Dungeon image slots11/12 still belong exclusively to spells. */
        if(game.area?sprite_slot>=13:object_count>=16)break;
        if(i && (game.enemies[i-1].alive!=1||game_enemy_hidden(&game,i-1)))continue;
        Motion *m=&motion[i];int moving=m->elapsed<m->total;
        int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;
        if(x<-32||x>272||y<0||y>208)continue;
        unsigned behind_door=i&&closed_door_monster_hidden((unsigned)i-1);
        if(behind_door&&!infravision_frame){closed_door_skipped++;continue;}
        const uint8_t *base;int count,anim;
        if(i==0){
            unsigned variant=hero_render_variants[coop_active?coop_id:0];
            const HeroArt *hero=hero_art(game_hero_class(),variant);HeroPose pose;
            hero_pose(hero,game_attack_repeat[coop_active?coop_id:0],game.hp,m,frame,game.move_wait,game.attack_wait,game.attack_duration,game.hit_flash|(game.reaction_duration<<8)|(game.reaction_block<<16),game.swing_target,game_hero_class(),variant,&pose);
            base=pose.asset;count=pose.count;anim=pose.frame;
        }else{
            const MonsterArt *art=&monster_art[game.enemies[i-1].kind];
            if(game_spell_stoned(i-1))monster_stone_pose(i-1,m,&base,&count,&anim);
            else if(game_enemy_hit(i-1)){const HitArt *hit=&monster_hit_art[game.enemies[i-1].kind];base=hit->data;count=hit->count;anim=count-(int)game_enemy_hit(i-1);if(anim<0)anim=0;if(anim>=count)anim=count-1;m->dir=game.enemy_facing[i-1];}
            else if(game_enemy_special(&game,i-1))monster_special_pose(i-1,m,&base,&count,&anim);
            else if(game.enemy_attack[i-1]){base=art->a;count=art->attack;anim=(game_monster_attack_ticks(game.enemies[i-1].kind)-game.enemy_attack[i-1])*count/game_monster_attack_ticks(game.enemies[i-1].kind);m->dir=game.enemy_facing[i-1];}
            else if(moving){base=art->w;count=art->walk;anim=(m->elapsed*count)/(m->total?m->total:1);if(anim>=count)anim=count-1;}
            else{m->dir=game.enemy_facing[i-1];base=art->n;count=art->idle;anim=(int)idle_pose_frame(frame,(unsigned)count);
#if PERF_FAR_IDLE
             /* Render-only distant idle pose; action/hit/stone paths above
              * retain their exact frames and simulation clocks. */
             int dx=(int)game.enemies[i-1].x-game.x,dy=(int)game.enemies[i-1].y-game.y;
             if(dx*dx+dy*dy>36&&i-1!=game.target)anim=0;
#endif
            }
        }
        int target=game.attack_wait?game.swing_target:game.target;
        int selected=!coop_active && i && i-1==target;
        unsigned bank=i?monster_palette_bank(game.enemies[i-1].kind,selected):hero_palette_bank(hero_render_variants[coop_active?coop_id:0]==27?3:game_hero_class());
        int lift=i||hero_render_variants[coop_active?coop_id:0]==27?63:60;
        actor_infra_tint=i&&infravision_frame&&(behind_door||lighting_at(m->x>>8,(m->y>>8)-16));
        int slot=crowd_actor_frame(&sprite_slot,base,m->dir,anim,count,(m->x>>8)-32,(m->y>>8)-lift,m->y>>8,&bank,i!=0);
        actor_infra_tint=0;
        if(slot<0)continue;
        add_obj(x-32,y-lift,m->y>>8,slot*64,bank,0,3);
    }
    draw_dormant_monsters(&sprite_slot);
    draw_town_npcs(frame,&sprite_slot);
    for(int i=sprite_slot;i<13;i++)d_raw_ref[i]=255;
    death_sprite_coop_plan((unsigned)sprite_slot);
    death_sprite_draw();draw_spell_effects(frame);
    /* Old saves keep their original collision map and a small exit marker. */
    if(!game.area && !game.map[3][0])add_obj(248-cam_x,88-cam_y,90,880,3,0,1);
    for(int i=1;i<object_count;i++){
        Obj v=objects[i];int j=i;while(j>0&&objects[j-1].depth<v.depth){objects[j]=objects[j-1];j--;}objects[j]=v;
    }
    for(int i=0;i<128;i++)oam[i*4]=0x0200;
    gameplay_hud();
    for(int i=0;i<object_count;i++){
        int n=i+(game.area?TOWN_ACTOR_OAM:42);
        Obj *o=&objects[i];oam[n*4]=(uint16_t)((o->y&255)|(o->x_pal_shape&0xe000));
        oam[n*4+1]=(uint16_t)((o->x_pal_shape&511)|(o->tile_size&0xc000));
        oam[n*4+2]=(uint16_t)((o->tile_size&1023)|(2<<10)|(((o->x_pal_shape>>9)&15)<<12));oam[n*4+3]=0;
    }
    /* Affine matrix0: double-size16px OBJ ->32px bounds, no extra tiles. */

    /* Prepare bounded map jobs before the actor-upload VBlank wait so the
     * work uses otherwise idle scanlines, rather than extending VBlank. */
    game_explore_update(&game);automap_draw();
    automap_arrow();
    /* Present this completed frame once, at the main loop's next VBlank.
     * Do not wait here and then wait for another VBlank in main. */
    frame_prepare_commit();frame_state|=1;profile_time(0,profile_start);
}
/* D8: run the deadline-critical upload loop in IWRAM. One shared publisher
 * avoids duplicated address/DMA code; no extra tile or queue memory is used.
 * The less frequent settled-decal decoder moves to ROM to pay for this space. */
static __attribute__((noinline)) void terrain_publish(unsigned slot,const void *pixels){
 unsigned tile=world_tile(slot);
 if(light_tiles[slot]&128)pixels=SOLO_TERRAIN_STAGE+(slot&63)*64;
 dma(pixels,(void*)(0x06000000+tile*64),64);
 /* The prepared map is published once, together with camera/OAM. */
 light_tiles[slot]&=(uint8_t)~(32|128);
}
static __attribute__((noinline)) void terrain_commit(void){
 if(game.area||!terrain_upload_count)return;
 if(coop_active){
  unsigned n=terrain_upload_count<COOP_TERRAIN_STAGE_TILES?terrain_upload_count:COOP_TERRAIN_STAGE_TILES;
  for(unsigned j=0;j<n;j++)terrain_publish(COOP_TERRAIN_SLOTS[j],COOP_TERRAIN_STAGE+j*64);
  if(n==terrain_upload_count)return;
 }
 for(unsigned i=0;i<WORLD_TILES;i++)if(light_tiles[i]&32)terrain_publish(i,TERRAIN_CACHE+i*64);
}
/* The scan-following inner loop must execute from IWRAM. GamePak instruction
 * fetches for651 small jobs can otherwise span an entire extra video frame. */
static __attribute__((noinline)) void terrain_repeat_row(unsigned row,unsigned column){
 unsigned col=column;
 for(unsigned x=0;x<31;x++){
  unsigned slot=row+col;if(++col==31)col=0;
  if(light_tiles[slot]&32){
   unsigned index=coop_active?COOP_TERRAIN_INDEX[slot]:0;
   terrain_publish(slot,index?COOP_TERRAIN_STAGE+(index-1)*64:TERRAIN_CACHE+slot*64);
  }
 }
}
/* For a missed/oversized transfer, repeat the old visible frame. Follow the
 * scanout: each old ring row stays untouched until its last visible scanline
 * has finished. All651 slots occur exactly once, including the offscreen edge.
 * The map, camera, palette and OAM remain old until the following VBlank.
 * This gives large jobs a bounded whole-frame window, without blanking BG0 or
 * waiting forever for the single admission line160. Audio IRQs stay enabled. */
static UI_FN void frame_repeat_transfer(void){
 uint8_t ends[13]={0};
 /* Actual live OAM, not the pending plan. Include every reference to a shared
  * image, and both2KiB halves of an8bpp corpse. No affine world images exist. */
 for(unsigned i=0;i<128;i++){
  unsigned a=R16(0x07000000+i*8),b=R16(0x07000002+i*8),t=R16(0x07000004+i*8)&1023;
  if((a&0x300)==0x200||t>=13*64)continue;
  unsigned shape=a>>14,size=b>>14;
  static const uint8_t widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
  static const uint8_t heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
  if(shape>=3)continue;
  int y=(int)(a&255);if(y>=160)y-=256;
  int end=y+heights[shape][size];if(end<=0)continue;if(end>160)end=160;
  unsigned last=(t+(widths[shape][size]*heights[shape][size]/((a&0x2000)?32:64))-1)/64;
  for(unsigned slot=t/64;slot<=last&&slot<13;slot++)if(ends[slot]<end)ends[slot]=(uint8_t)end;
 }
 /* Start at the next visible scan, keeping the complete old image on screen. */
 while(R16(0x04000006)>=160){}
 unsigned row=render_row(render_tile(presented_y)),column=render_column(render_tile(presented_x));
 for(unsigned y=0;y<21;y++){
  unsigned end=(y+1)*8-(presented_y&7);if(end>160)end=160;
  while(R16(0x04000006)<end){}
  /* Earlier rows have finished scanning. A new coordinate never changes
   * the live map here; ring slots are still referenced at their OLD position. */
  terrain_repeat_row(row,column);
  row+=31;if(row==WORLD_TILES)row=0;
  for(unsigned slot=0;slot<13;slot++)if((DEATH_SPRITE_STATE->pending&(1u<<slot))&&ends[slot]<=end)
   death_sprite_commit_one(slot);
 }
 /* The last row may be followed immediately by the VBlank HUD IRQ. The
  * remaining work is bounded: a2KiB map and palette/OAM/register publication. */
 while(R16(0x04000006)<160){}
}
static UI_FN void frame_commit(void){
    if(!(frame_state&1))return;
    unsigned death_upload=death_sprite_upload_pending(),terrain=terrain_upload_count;
    unsigned line;do{line=R16(0x04000006);}while(line<160||line>198);
    unsigned profile_start=audio_game_clock();
    if(!game.area&&(terrain||death_upload||effect_tile_cache.pinned)&&
       (frame_oversized||line>frame_deadline)&&(R16(0x04000000)&0x0100)){
        frame_repeat_transfer();
    }else{
        terrain_commit();
        if(death_upload)death_sprite_commit_tiles();
    }
    if(!game.area)effect_tiles_commit_range(0,16);
    if(!(R16(0x04000000)&0x0100)){
        unsigned line;do{line=R16(0x04000006);}while(line<160||line>198);
    }
    if(!game.area&&terrain)dma(FRAME_MAP,(void*)0x0600e000,2048);
    if(actor_palette_pending())actor_palette_commit();
    R16(0x04000010)=(uint16_t)cam_x;R16(0x04000012)=(uint16_t)cam_y;
    if(!game.area){presented_x=(int16_t)cam_x;presented_y=(int16_t)cam_y;}
    dma(oam,(void*)0x07000000,1024);death_sprite_commit_end();
    lighting_hardware_commit();
    R16(0x04000000)=(0x3340|(R16(0x04000000)&0x0400))|(lighting_hardware_active()?0x4000:0)
        |outer_mask_gameplay();
    if(R16(0x04000054)!=16)automap_blend();
    frame_state=(frame_state+2)&~1u;profile_time(6,profile_start);
}
#include "menu.inc"
#include "quick_popup.inc"
#include "link_diag_ui.inc"
#include "coop_campaign.inc"
#include "coop_runtime.inc"
#include "frontend.inc"
static UI_FN void quest_redraw(void){
 ++crowd_mask_epoch;
 closed_door_cull_valid=0;door_graph_state=0;
 /* Terrain seals change contours, not just explored fog. Retire any queued
  * old-map upload and rebuild on the next visible automap draw. */
 map_last_x=-1;map_init=1;map_pending=0;
 if(dungeon_level>4)game_world_doors_overlay(&game,town_map,(uint8_t*)town_map+12800);
 for(unsigned i=0;i<6400;i++)PROP_LOOKUP[i]=0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(game.props[i].kind)PROP_LOOKUP[game.props[i].y*80+game.props[i].x]=i+1;
 if(dungeon_level>4)fixed_world_walls_prepare();
 for(int i=0;i<WORLD_TILES;i++){terrain_invalidate(i);ring_y[i]=RING_INVALID;}
 for(int i=0;i<MAX_PROPS;i++)prop_draw_frames[i]=255;
}
static UI_FN void valor_refresh(unsigned old_stage){
 if(dungeon_level==7&&quest_progress.stage[8]){
  int x=quest_valor_cell%80,y=quest_valor_cell/80;
  game_rooms_live_rect(x+17,y+17,x+40,y+40,9);
 }
 game_world_valor_change(0,town_map,(uint8_t*)town_map+12800);
 quest_redraw();
 if(!old_stage){quest_notice=dungeon_level==7?8:7;menu_mode=11;menu_draw();}
}
static UI_FN void banner_refresh(unsigned stage){
 cathedral_rooms_refresh();
 game_dungeon_visuals(&game,(uint16_t*)((uint8_t*)town_map+DUNGEON_LAYOUT_OFFSET),0);
 quest_redraw();
 quest_notice=stage==4?15:14;menu_mode=11;menu_draw();
}
static UI_FN unsigned late_revision(void){return (quest_progress.stage[11]&63)+(quest_progress.stage[14]<<4)+(quest_progress.bone_gates<<8);}
static UI_FN int late_refresh(unsigned old_level,unsigned old_revision){
 if(old_level==dungeon_level&&!game.area&&old_revision!=late_revision()){
  game_late_change(0,town_map,(uint8_t*)town_map+12800);
  quest_redraw();
 }
 if(!game.area&&game.town_message==42){game.town_message=0;confirm_action=game_reward_candidate(&game);confirm_yes=1;menu_mode=13;menu_draw();return 1;}
 if(!game.area&&game.town_message==43){game.town_message=0;menu_notice="CLEAR GROUND FOR REWARD";menu_mode=14;menu_draw();return 1;}
 if(!game.area&&game.town_message>=31&&game.town_message<=52){quest_notice=game.town_message;game.town_message=0;menu_mode=11;menu_draw();return 1;}
 return 0;
}
/* Cold quest geometry work stays in ROM; speech-only saved flags never redraw
 * the dungeon or consume the tightly bounded gameplay IWRAM allocation. */
static UI_FN unsigned finale_visual_refresh(unsigned old_level,unsigned old_flags){
 if(game.area||old_level!=dungeon_level||!((old_flags^quest_progress.finale_flags)&BOSS_GEOMETRY_FLAGS))return 0;
 if(dungeon_level==REGION_VILE)game_vile_change(0,town_map,(uint8_t*)town_map+12800);
 else if(dungeon_level==16)game_diablo_change(0,town_map,(uint8_t*)town_map+12800);
 quest_redraw();
 if(dungeon_level==REGION_VILE){place(&motion[0],game.x,game.y);return 1;}
 return 0;
}
/* Keep SP actor snapshots off the persistent frontend/co-op stack. */
typedef struct {
 unsigned pending,frame,animation_frame,timer_last,cycle_remainder;
 unsigned entry_voice,entry_delay;
} FrameState;
/* Town dialogue runs only on an interaction, so keep it outside the hot
 * IWRAM frame loop. Preserve talk/shop/heal/quest side-effect order. */
static UI_FN unsigned singleplayer_talk_npc(void){
    int n=nearby_npc();if(n<0)return 0;
    game.town_message=(uint8_t)(n+1);game.pickup_notice_ticks=60;
    if(n>=9){audio_cow(game_hero_class());return 1;}
    unsigned wounded_first=n==2&&butcher_quest==1;
    int q=game_quest_talk(&game,n);
    if(n==6){game.hp=game_max_hp();hero_progress.life_steal_fraction=0;}
    if(n==2&&butcher_quest==1){butcher_quest=2;game.pickup_notice_ticks=160;}
    dialogue_open((unsigned)n,(unsigned)q,wounded_first);return 1;
}
/* Preserve the existing hot loop's IWRAM placement; no .text.ui attribute. */
static UI_FN void singleplayer_run_frame(unsigned held,unsigned pressed,unsigned elapsed,FrameState *state){
    if(menu_mode){
        quick_mode=0;quick_blocked&=held;
        state->pending=0;menu_input(pressed,&state->animation_frame);quick_blocked|=held;
        if(quick_mode)quick_draw();else if(!menu_mode)draw(state->animation_frame);
        else if(menu_mode<=6||menu_mode==15)menu_canvas_draw();
        state->timer_last=audio_game_clock();state->cycle_remainder=0;graphics_frames++;return;
    }
    unsigned popup_input=quick_input(held,pressed);
    if(popup_input){
        state->pending=0;
        if(quick_mode)quick_draw();else if(menu_mode)menu_draw();else draw(state->animation_frame);
        state->timer_last=audio_game_clock();state->cycle_remainder=0;graphics_frames++;return;
    }
    held&=~quick_blocked;pressed&=~quick_blocked;
    if(menu_mode){
        state->pending=0;menu_input(pressed,&state->animation_frame);quick_blocked|=held;
        state->timer_last=audio_game_clock();state->cycle_remainder=0;graphics_frames++;return;
    }
    held=options_dpad_keys(held,dpad_rotation);
    pressed=options_dpad_keys(pressed,dpad_rotation);
    state->pending|=pressed&~(KEY_SELECT|KEY_START);
    for(unsigned step=0;step<elapsed;step++){
    if(state->frame%3==0){
        int ox=game.x,oy=game.y,old_hp=game.hp;uint8_t ex[MAX_ENEMIES],ey[MAX_ENEMIES];int16_t old_enemy_hp[MAX_ENEMIES];uint8_t alive[MAX_ENEMIES],old_attack[MAX_ENEMIES];
        for(int i=0;i<MAX_ENEMIES;i++){ex[i]=game.enemies[i].x;ey[i]=game.enemies[i].y;alive[i]=game.enemies[i].alive;old_enemy_hp[i]=game.enemies[i].hp;old_attack[i]=game.enemy_attack[i];}
        unsigned old_area=game.area,old_level=dungeon_level,old_hero_level=hero_progress.level,old_valor=quest_progress.stage[dungeon_level==7?8:9],old_veil=quest_progress.stage[4]&63,old_banner=quest_progress.stage[7],old_king=quest_progress.king_gates,old_water=quest_progress.state[13],old_finale=quest_progress.finale_flags,old_laz=quest_progress.stage[15],old_won=game.won;unsigned action=state->pending;
        unsigned old_late=late_revision(),old_visited=quest_progress.visited;
        game_step(&game,held,state->pending);state->pending=0;
        if(late_refresh(old_level,old_late))elapsed=step+1;
        if(!game.area&&(dungeon_level==5||dungeon_level==7)&&old_level==dungeon_level&&old_valor!=quest_progress.stage[dungeon_level==7?8:9]){
            valor_refresh(old_valor);if(menu_mode==11)elapsed=step+1;
        }
        water_refresh(old_water);
        if(finale_visual_refresh(old_level,old_finale)){ox=game.x;oy=game.y;}
        if(dungeon_level==REGION_VILE&&old_laz!=6&&quest_progress.stage[15]==6){quest_notice=24;menu_mode=11;menu_draw();elapsed=step+1;}
        if(!old_won&&game.won)elapsed=step+1;
        if(!game.area&&dungeon_level==REGION_KING&&old_king!=quest_progress.king_gates){game_king_change(0,town_map,(uint8_t*)town_map+12800);quest_redraw();}
        if(!game.area&&dungeon_level==4&&old_banner!=quest_progress.stage[7]){banner_refresh(quest_progress.stage[7]);elapsed=step+1;}
        if(!game.area&&dungeon_level==14&&old_veil!=(quest_progress.stage[4]&63)){quest_notice=(quest_progress.stage[4]&63)==3?10:9;menu_mode=11;menu_draw();elapsed=step+1;}
        if(old_hp>0&&game.hp<=0&&!(!game.area&&dungeon_level==16&&quest_progress.state[5]==3)){dialogue_end();inventory_release();quick_mode=0;draw(state->animation_frame);frame_commit();menu_mode=1;menu_choice=3;elapsed=step+1;}
        if(game.area && old_area && !game.paused && !game.move_wait && game.target!=TARGET_ITEM && (action&KEY_A)){
            if(singleplayer_talk_npc())elapsed=step+1;
        }
        if(region_error){region_error=0;load_scene();reset_visuals();draw(state->animation_frame);reveal_scene();menu_mode=4;menu_notice="REGION SAVE FAILED";menu_draw();state->timer_last=audio_game_clock();state->cycle_remainder=0;elapsed=step+1;}
        else if(old_area!=game.area||old_level!=dungeon_level){
            state->entry_delay=0;
            if(!game.area&&dungeon_level<=16&&!(old_visited&(1u<<(dungeon_level-1)))){
                unsigned level=dungeon_level;
                if(level==1||level==5||level==9||level==13||level==16){
                    state->entry_voice=level==16?268:256+game_hero_class()*4+(level-1)/4;
                    state->entry_delay=120;
                }
            }
            load_scene();reset_visuals();draw(state->animation_frame);reveal_scene();state->timer_last=audio_game_clock();state->cycle_remainder=0;elapsed=step+1;}
        else{
            if(!game.area){
                /* Swing precedes impact, matching PM_DoAttack's AFNum-1 cue. */
                if(game.attack_wait&&!SPELL_CASTING(game.swing_target)&&!game_attack_bow(game.attack_duration)&&game.attack_wait==game_attack_hit_wait(game_hero_class(),game.attack_duration)+1)audio_effect(0,1);
                for(int i=0;i<MAX_ENEMIES;i++)if(alive[i]==1){
                    int dx=game.enemies[i].x-game.x,dy=game.enemies[i].y-game.y;
                    if(dx<-6||dx>6||dy<-6||dy>6)continue;
                    unsigned kind=game.enemies[i].kind;
                    if(game.enemies[i].hp<old_enemy_hp[i]){if(kind!=MONSTER_DIABLO||game.enemies[i].alive)audio_monster(kind,game.enemies[i].alive?1:2);}
                    else if(!old_attack[i]&&game.enemy_attack[i])audio_monster(kind,0);
                }
                if(old_hp>0&&game.hp<=0)audio_hero_death(game_hero_class());
                else if(game.hp<old_hp)audio_effect(1+game_hero_class(),4);
                if(hero_progress.level>old_hero_level)audio_effect(41,5);
            }
            /* Publish loot from this simulation tick beside its death cue. */
            item_sound_update();
            move_to(&motion[0],game.x,game.y,ox,oy,WALK_TICKS*3);
            motion[0].dir=game.facing;
            for(int i=0;i<MAX_ENEMIES;i++){if(!alive[i]&&game.enemies[i].alive)place(&motion[i+1],game.enemies[i].x,game.enemies[i].y);else if(game.enemies[i].alive&&(game.enemies[i].x!=ex[i]||game.enemies[i].y!=ey[i])){if(game_enemy_hit(i))place(&motion[i+1],game.enemies[i].x,game.enemies[i].y);else move_to(&motion[i+1],game.enemies[i].x,game.enemies[i].y,ex[i],ey[i],game_enemy_walk_ticks(&game,i)*3);}}
        }
    }
    if(!game.paused&&game.hp>0&&!game.won){
        for(int i=0;i<MAX_ENEMIES+1;i++)if(motion[i].elapsed<motion[i].total&&(!i||(game.enemies[i-1].alive&&!game_enemy_hit(i-1)&&!game_spell_stoned(i-1))))advance(&motion[i]);state->animation_frame++;
    }
    if(state->entry_delay){
        if(game.area||game.hp<=0)state->entry_delay=0;
        else if(!game.paused&&!--state->entry_delay&&!audio_boss_busy())audio_speech_start(state->entry_voice,0,0);
    }
    state->frame++;
    }
    projectile_phase=state->frame%3;
    if(menu_mode)menu_draw();else draw(state->animation_frame);graphics_frames++;
}
#include "ending.inc"
UI_FN int main(void){
    R16(0x04000204)=0x4317;display_black();
    R16(0x0400010C)=0;R16(0x0400010E)=0x0080;
    unsigned prev=0;FrameState state={0};
    R16(0x04000100)=0;R16(0x04000102)=0x0083;
    frontend_enter(&state.animation_frame,1);
    prev=(~R16(0x04000130))&1023;
    /* Timer 0 /1024 measures elapsed hardware time, independently of render cost. */
    R16(0x04000100)=0;R16(0x04000102)=0x0083;
    while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
    state.timer_last=audio_game_clock();
    for(;;){
        if(menu_mode==5){state.entry_delay=0;quick_mode=quick_blocked=0;frontend_enter(&state.animation_frame,0);prev=(~R16(0x04000130))&1023;state.pending=0;state.timer_last=audio_game_clock();state.cycle_remainder=0;}
        while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
        frame_commit();automap_commit();item_sound_update();audio_update();boss_sound_update();hero_sound_update();
        if(spell_audio_seen!=spell_audio_event){spell_audio_seen=spell_audio_event;audio_spell(spell_audio_seen&255);}
        unsigned now=audio_game_clock();
        state.cycle_remainder+=(uint16_t)(now-state.timer_last)*1024u;state.timer_last=now;
        unsigned elapsed=state.cycle_remainder/280896u;state.cycle_remainder%=280896u;
        /* A busy draw must not cap the 20Hz simulation to four video frames.
         * Catch up through 200ms; retain longer debt instead of losing time. */
        if(elapsed>12){state.cycle_remainder+=(elapsed-12)*280896u;elapsed=12;}
        unsigned held=(~R16(0x04000130))&1023;
        unsigned pressed=held&~prev;prev=held;
        if(active_game&&game.won){ending_enter(state.animation_frame);prev=(~R16(0x04000130))&1023;state.pending=0;state.timer_last=audio_game_clock();state.cycle_remainder=0;continue;}
        if(coop_active){coop_run_frame(held,pressed);state.timer_last=audio_game_clock();state.cycle_remainder=0;continue;}
        singleplayer_run_frame(held,pressed,elapsed,&state);
    }
}
