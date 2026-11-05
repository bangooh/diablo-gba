/* Nonzero only during a local shortcut transaction; never saved directly. */
static uint8_t region_shortcut;
/* Devilution single-player menu order; native GBA Flash and display adapters.
 * Included after draw() so the menu shares the existing font and scene loader. */
#include "flash.h"
#include "shop.h"
#include "prop_archive.h"
#include "coop.h"
#include "options.h"
static SmithStock town_smith,town_other[VENDOR_COUNT-1];
static PremiumRows town_premium;
#include "shop_session.inc"
#include <stddef.h>
volatile unsigned menu_mode; /* 0 game, 1 main, 2 options, 3 confirm, 4 notice, 5 title */
static unsigned menu_choice,confirm_action,confirm_yes,active_game=1;
static const char *menu_notice;
static unsigned session_hero=1,quest_notice,new_difficulty;
static uint8_t dialogue_session;
static UI_FN void inventory_release(void);
static UI_FN void menu_canvas_draw(void);
/* Set when a region transaction fails; the live region remains intact. */
static unsigned region_error;
/* The archive has 20 fixed region slots in Flash, but only two staging slots in
 * RAM. Terrain is deterministic generator v17; doors live in Prop.state. */
#define SAVE_VERSION 131u
#define SAVE_GENERATOR 22u
#define SAVE_FLOORS 20u
#define PLAYABLE_FLOORS 16u
typedef struct {uint8_t position[3],payload[12];} SavedDrop;
typedef struct {
 CharacterState hero;
 struct { int16_t x,y; uint8_t area,facing; uint16_t reserved; } location;
 Motion player;
 uint32_t gamma,animation,seed,quest,generated,generator,history[MONSTER_HISTORY_TYPES];
 SmithStock smith,other[VENDOR_COUNT-1];PremiumRows premium;QuestProgress quests;uint32_t shop_reserved,metadata_reserved,metadata_padding,metadata_padding2,metadata_padding3;SavedDrop town_drops[TOWN_DROPS];
} SaveMeta;
/* Coordinates fit 0..79 and kinds/alive fit six/two bits. Preserve every
 * enemy, drop and transient field; only the archive representation changes. */
typedef struct { uint8_t position[2],hp[2],kind,maximum; } SavedEnemy;
/* Coordinates are7 bits, active1 bit, item kind8 bits: three bytes.
 * Normal props use7+7+6+4 bits; wall traps use a coordinate escape to
 * preserve their six-bit target slot and spent bit. Other bytes are unchanged. */

typedef struct {uint8_t data[3];} SavedProp;
#define FLOOR_MIDDLE_OFFSET offsetof(Game,x)
#define FLOOR_MIDDLE_BYTES (offsetof(Game,props)-FLOOR_MIDDLE_OFFSET)
#define FLOOR_END_OFFSET offsetof(Game,down_x)
typedef struct {SavedEnemy enemies[MAX_ENEMIES];SavedDrop drops[MAX_DROPS];uint8_t middle[FLOOR_MIDDLE_BYTES-MAX_ENEMIES];SavedProp props[MAX_PROPS];uint8_t tail[sizeof(Game)-FLOOR_END_OFFSET],explored[200];} FloorState;
_Static_assert(ITEM_COUNT<=256,"eight-bit saved item kind");
_Static_assert(MONSTER_TYPES<=256,"eight-bit archived monster kind");
typedef struct {
 /* Keep the named metadata fields available to frontend/menu callers. */
 CharacterState hero;
 typeof(((SaveMeta*)0)->location) location;
 Motion player;
 uint32_t gamma,animation,seed,quest,generated,generator,history[MONSTER_HISTORY_TYPES];
 SmithStock smith,other[VENDOR_COUNT-1];PremiumRows premium;QuestProgress quests;uint32_t shop_reserved,metadata_reserved,metadata_padding,metadata_padding2,metadata_padding3;SavedDrop town_drops[TOWN_DROPS];
 FloorState floor,outgoing;
} SavePayload;
_Static_assert(offsetof(SavePayload,floor)==sizeof(SaveMeta),"archive metadata layout");
_Static_assert(sizeof(SavePayload)<=sizeof(load_buffer),"save scratch capacity");
#define SAVE_BYTES (sizeof(SaveMeta)+SAVE_FLOORS*sizeof(FloorState))
_Static_assert(SAVE_BYTES+FLASH_HEADER_BYTES<=FLASH_BANK_BYTES,"atomic 20-region archive capacity");
#define save_buffer (*(SavePayload*)load_buffer)
#define region_stage (*(Game*)((uint8_t*)town_map+128000))
static UI_FN unsigned floor_offset(unsigned level){return sizeof(SaveMeta)+(level-1)*sizeof(FloorState);}
static UI_FN void clear_save_buffer(void){for(unsigned i=0;i<sizeof(save_buffer);i++)((uint8_t*)&save_buffer)[i]=0;save_buffer.generator=SAVE_GENERATOR;}
static UI_FN void save_location(const Game *g){
 save_buffer.location.x=g->x;save_buffer.location.y=g->y;save_buffer.location.area=g->area;save_buffer.location.facing=g->facing;save_buffer.location.reserved=dungeon_level;
}
static UI_FN uint32_t saved_history(unsigned i){return save_buffer.history[i];}
static UI_FN void capture_history(void){for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)save_buffer.history[i]=monster_history[i];}
/* The remaining Flash bank space holds40 persistent town drops. Keep this
 * in archive scratch, not scarce permanent GBA RAM. */
static UI_FN void town_drops_capture(const Game *g){
 unsigned n=0;for(unsigned i=0;i<MAX_DROPS&&n<TOWN_DROPS;i++)if(g->drops[i].active){
  const Drop *d=&g->drops[i];SavedDrop *out=&save_buffer.town_drops[n++];
  unsigned bits=d->x|((unsigned)d->y<<7)|(1u<<14)|((unsigned)d->kind<<15);
  for(unsigned j=0;j<3;j++)out->position[j]=bits>>(8*j);
  for(unsigned j=0;j<12;j++)out->payload[j]=((const uint8_t*)d)[4+j];
 }
 for(;n<TOWN_DROPS;n++)save_buffer.town_drops[n]=(SavedDrop){0};
}
static UI_FN void town_drops_apply(Game *g){
 for(unsigned i=0;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};
 for(unsigned i=0;i<TOWN_DROPS;i++){
  const SavedDrop *s=&save_buffer.town_drops[i];unsigned bits=s->position[0]|((unsigned)s->position[1]<<8)|((unsigned)s->position[2]<<16);Drop *d=&g->drops[i];
  d->x=bits&127;d->y=(bits>>7)&127;d->active=(bits>>14)&1;d->kind=(bits>>15)&255;
  for(unsigned j=0;j<12;j++)((uint8_t*)d)[4+j]=s->payload[j];
 }
}
static UI_FN void save_metadata(void){if(game.area)town_drops_capture(&game);save_buffer.seed=campaign_seed;save_buffer.quest=butcher_quest;save_buffer.smith=town_smith;for(unsigned i=0;i<VENDOR_COUNT-1;i++)save_buffer.other[i]=town_other[i];save_buffer.premium=town_premium;save_buffer.quests=quest_progress;save_buffer.shop_reserved=0;save_buffer.metadata_padding=options_volume_pack(audio_music_enabled?audio_music_volume:0,audio_effects_volume);save_buffer.metadata_padding3=options_dpad_pack(dpad_rotation);save_buffer.metadata_reserved=coop_campaign_epoch;save_buffer.metadata_padding2=game_difficulty;capture_history();}
/* Live goats retain9-bit HP and maxHP in the coordinate high bits.
 * Legal tile coordinates need only7 bits. Dead records keep signed16 HP.
 * Retail Hell multiplayer Gharbad is483 HP; the five-byte size is unchanged. */
static UI_FN __attribute__((minsize)) int floor_capture(FloorState *dst,const Game *g){
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const Enemy *e=&g->enemies[i];unsigned maximum=game_monster_hp_archive(e->kind,e->max_hp);
  if(e->x>=80||e->y>=80||e->kind>=MONSTER_TYPES||e->alive>2||maximum>255||g->enemy_attack[i]>31||g->enemy_facing[i]>7)return 0;
  unsigned position=e->x|((unsigned)e->y<<7)|((unsigned)e->alive<<14);
  dst->enemies[i]=(SavedEnemy){{position,position>>8},{e->hp,(uint16_t)e->hp>>8},e->kind,maximum};
 }
 for(unsigned i=0;i<MAX_DROPS;i++){
  const Drop *d=&g->drops[i];unsigned bits=d->x|((unsigned)d->y<<7)|((unsigned)d->active<<14)|((unsigned)d->kind<<15);
  for(unsigned j=0;j<3;j++)dst->drops[i].position[j]=bits>>(8*j);
  for(unsigned j=0;j<12;j++)dst->drops[i].payload[j]=((const uint8_t*)d)[4+j];
 }
 for(unsigned i=0;i<MAX_PROPS;i++){const Prop *p=&g->props[i];unsigned bits=game_prop_archive_pack(p);for(unsigned j=0;j<3;j++)dst->props[i].data[j]=bits>>(8*j);}
 unsigned at=0;
 for(unsigned i=FLOOR_MIDDLE_OFFSET;i<offsetof(Game,props);i++){
  if(i>=offsetof(Game,enemy_facing)&&i<offsetof(Game,enemy_facing)+MAX_ENEMIES)continue;
  unsigned value=((const uint8_t*)g)[i];
  if(i>=offsetof(Game,enemy_attack)&&i<offsetof(Game,enemy_attack)+MAX_ENEMIES)value|=g->enemy_facing[i - offsetof(Game,enemy_attack)]<<5;
  dst->middle[at++]=value;
 }
 for(unsigned i=0;i<sizeof(dst->tail);i++)dst->tail[i]=((const uint8_t*)g)[FLOOR_END_OFFSET+i];
 game_explore_export(dst->explored);
 return 1;
}
static UI_FN __attribute__((minsize)) void floor_apply(Game *g,const FloorState *src){
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const SavedEnemy *e=&src->enemies[i];unsigned position=e->position[0]|((unsigned)e->position[1]<<8);
  unsigned kind=e->kind<MONSTER_TYPES?e->kind:0;
  g->enemies[i]=(Enemy){position&127,(position>>7)&127,(int16_t)(e->hp[0]|((unsigned)e->hp[1]<<8)),position>>14,kind,game_monster_hp_unarchive(kind,e->maximum)};
  /* Older saves may contain an untouched boss with the old literal HP and
   * no maximum. Repair only that exact live record, never wounded/dead bosses. */
  Enemy *restored=&g->enemies[i];
  if(restored->alive&&!restored->max_hp&&
     ((kind==MONSTER_BUTCHER&&restored->hp==110)||(kind==MONSTER_KING&&restored->hp==120))){
   restored->hp=game_monster_hp(kind,campaign_seed);restored->max_hp=restored->hp;
  }
 }
 for(unsigned i=0;i<MAX_DROPS;i++){
  const SavedDrop *d=&src->drops[i];unsigned bits=d->position[0]|((unsigned)d->position[1]<<8)|((unsigned)d->position[2]<<16);
  Drop *p=&g->drops[i];p->x=bits&127;p->y=(bits>>7)&127;p->active=(bits>>14)&1;p->kind=(bits>>15)&255;
  for(unsigned j=0;j<12;j++)((uint8_t*)p)[4+j]=d->payload[j];
 }
 for(unsigned i=0;i<MAX_PROPS;i++){const uint8_t *d=src->props[i].data;unsigned bits=d[0]|((unsigned)d[1]<<8)|((unsigned)d[2]<<16);g->props[i]=game_prop_archive_unpack(bits);}
 unsigned at=0;
 for(unsigned i=FLOOR_MIDDLE_OFFSET;i<offsetof(Game,props);i++){
  if(i>=offsetof(Game,enemy_facing)&&i<offsetof(Game,enemy_facing)+MAX_ENEMIES)continue;
  unsigned value=src->middle[at++];
  if(i>=offsetof(Game,enemy_attack)&&i<offsetof(Game,enemy_attack)+MAX_ENEMIES){g->enemy_facing[i - offsetof(Game,enemy_attack)]=value>>5;value&=31;}
  ((uint8_t*)g)[i]=value;
 }
 for(unsigned i=0;i<sizeof(src->tail);i++)((uint8_t*)g)[FLOOR_END_OFFSET+i]=src->tail[i];
}
static UI_FN int floor_read(unsigned level,FloorState *out){return level>=1&&level<=SAVE_FLOORS&&flash_save_read_range(out,SAVE_BYTES,SAVE_VERSION,floor_offset(level),sizeof(*out));}
static UI_FN int save_metadata_valid(const SaveMeta *meta){
 const CharacterState *h=&meta->hero;const typeof(meta->location) *g=&meta->location;int limit=g->area?96:80;
 if(g->area>1||g->facing>7||g->x<0||g->y<0||g->x>=limit||g->y>=limit||h->hp<=0||h->mana<0||h->max_mana<0||h->mana>h->max_mana||h->gold<0||!game_belt_valid(h->belt,h->potions)||meta->gamma>=OPTIONS_GAMMA_LEVELS)return 0;
 if((h->reserved&0xff00)!=0x4300||(h->reserved&255)>2||!game_progress_valid(h)||!items_valid(&h->inventory))return 0;
 if(g->reserved<1||g->reserved>REGION_BONE||(g->reserved==REGION_KING&&!meta->quests.state[12])||(g->reserved==REGION_WATER&&!meta->quests.state[13])||meta->quest>3||meta->generated>>SAVE_FLOORS||meta->generator!=SAVE_GENERATOR)return 0;
 for(unsigned i=0;i<TOWN_DROPS;i++){const uint8_t *p=meta->town_drops[i].position;unsigned bits=p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16);if((bits&127)>=96||((bits>>7)&127)>=96||(bits>>23)||((bits&(1u<<14))&&((bits>>15)&255)>=ITEM_COUNT))return 0;}
 if(!game_quest_valid(&meta->quests))return 0;
 if(meta->metadata_padding2>=GAME_DIFFICULTY_COUNT||!options_volume_valid(meta->metadata_padding)||meta->shop_reserved||!smith_stock_valid(&meta->smith)||meta->smith.level<6||meta->smith.level>16)return 0;
 for(unsigned i=1;i<VENDOR_COUNT;i++)if(!vendor_valid(i,&meta->other[i-1]))return 0;
 if(!premium_valid(&meta->other[VENDOR_PREMIUM-1],&meta->premium))return 0;
 if(!g->area&&!(meta->generated&(1u<<(g->reserved-1))))return 0;
 return 1;
}
static UI_FN int save_read(void){
 for(int i=0;i<4;i++)raw_base[i]=0;
 return flash_save_read_range(&save_buffer,SAVE_BYTES,SAVE_VERSION,0,sizeof(SaveMeta))&&save_metadata_valid((const SaveMeta*)&save_buffer);
}
static UI_FN void floor_generate(Game *s,unsigned level){
 for(unsigned i=0;i<sizeof(*s);i++)((uint8_t*)s)[i]=0;
 unsigned old=dungeon_level;uint8_t *scratch=dungeon_workspace;
 dungeon_workspace=(uint8_t*)town_map;dungeon_level=level;
 uint32_t seed=game_floor_seed(campaign_seed,level);
 game_cave_policy=2;
 game_generate_dungeon(s,seed);
#ifndef FLASH_HOST_TEST
 cave_handoff_prepare(level,seed);
#endif
 dungeon_level=old;dungeon_workspace=scratch;
}
static UI_FN int save_restore(Game *g){
 /* Loading artwork borrows load_buffer; preserve the small metadata header. */
 SaveMeta meta;for(unsigned i=0;i<sizeof(meta);i++)((uint8_t*)&meta)[i]=((uint8_t*)&save_buffer)[i];
 audio_stop();loading_begin(meta.location.area?6:loading_destination(0,meta.location.reserved));
 for(unsigned i=0;i<sizeof(meta);i++)((uint8_t*)&save_buffer)[i]=((uint8_t*)&meta)[i];
 if(!meta.location.area&&!floor_read(meta.location.reserved,&save_buffer.floor))return 0;
 game_spell_new_campaign();game_set_difficulty(save_buffer.metadata_padding2);coop_campaign_epoch=save_buffer.metadata_reserved;
 options_volume_apply(save_buffer.metadata_padding);dpad_rotation=options_dpad_unpack(save_buffer.metadata_padding3);
 dungeon_level=save_buffer.location.reserved;butcher_quest=save_buffer.quest;campaign_seed=save_buffer.seed;quest_progress=save_buffer.quests;town_premium=save_buffer.premium;town_smith=save_buffer.smith;for(unsigned i=0;i<VENDOR_COUNT-1;i++)town_other[i]=save_buffer.other[i];
 if(!save_buffer.location.area){
  floor_generate(g,dungeon_level);floor_apply(g,&save_buffer.floor);game_explore_import(save_buffer.floor.explored);
 }else {for(unsigned i=0;i<sizeof(*g);i++)((uint8_t*)g)[i]=0;town_drops_apply(g);game_explore_reset();}
 g->x=save_buffer.location.x;g->y=save_buffer.location.y;g->area=save_buffer.location.area;g->facing=save_buffer.location.facing;g->target=g->swing_target=-1;g->move_wait=g->attack_wait=g->hit_flash=0;g->attack_queued=0;g->attack_duration=0;g->reaction_duration=g->reaction_block=0;
 game_apply_character(g,&save_buffer.hero);game_quest_town(g);for(unsigned i=0;i<MONSTER_HISTORY_TYPES;i++)monster_history[i]=saved_history(i);game_unique_restore(g);game_finale_restore(g);return 1;
}
/* One commit may contain the outgoing floor and the destination floor. */
static UI_FN int archive_write(unsigned outgoing,unsigned destination,int reset){
 FlashPatch p[3]={{0,sizeof(SaveMeta),&save_buffer}};unsigned n=1;
 if(outgoing)p[n++]=(FlashPatch){floor_offset(outgoing),sizeof(FloorState),&save_buffer.outgoing};
 if(destination)p[n++]=(FlashPatch){floor_offset(destination),sizeof(FloorState),&save_buffer.floor};
 return flash_save_patch(SAVE_BYTES,SAVE_VERSION,p,n,reset);
}
static UI_FN int save_write(unsigned animation){
 if(!save_read())return 0;
 unsigned floor=game.area?0:dungeon_level;
 if(floor){if(!floor_capture(&save_buffer.floor,&game))return 0;save_buffer.generated|=1u<<(floor-1);}
 save_location(&game);save_metadata();game_get_character(&game,&save_buffer.hero);
 save_buffer.player=motion[0];save_buffer.gamma=gamma_level;save_buffer.animation=animation;
 if(game.won&&save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);
 if(!save_metadata_valid((const SaveMeta*)&save_buffer))return 0;audio_stop();
 save_buffer.metadata_reserved=coop_campaign_epoch+1;
 int ok=archive_write(0,floor,0);if(ok)coop_campaign_epoch=save_buffer.metadata_reserved;audio_start(audio_region_track(game.area,game_dungeon_depth()));return ok;
}
static UI_FN void save_player_position(int px,int py,unsigned area,unsigned facing){
 int x=((area?1536:DUNGEON_ORIGIN)+(px-py)*16)*256,y=((area?128:72)+(px+py)*8)*256;
 save_buffer.player=(Motion){x,y,x,y,x,y,0,0,facing};
}
static UI_FN void region_player_position(const Game *g){save_player_position(g->x,g->y,g->area,g->facing);}
static UI_FN void region_reset(Game *s){s->move_wait=s->attack_wait=s->hit_flash=0;s->attack_queued=0;s->attack_duration=0;s->reaction_duration=s->reaction_block=0;s->target=s->swing_target=-1;s->won=s->paused=s->town_message=0;}
static UI_FN int save_prepare_shops(int fresh){
  unsigned deepest=0;for(unsigned i=0;i<PLAYABLE_FLOORS;i++)if(save_buffer.generated&(1u<<i))deepest=i+1;
  unsigned shop_level=smith_store_level(deepest,0,0);
  if(fresh||shop_level>save_buffer.smith.level)smith_stock_init(&save_buffer.smith,item_seed(save_buffer.seed^0x53484f50u^shop_level),shop_level);
  for(unsigned v=1;v<VENDOR_COUNT;v++){
   unsigned target=(v==VENDOR_WITCH||v==VENDOR_HEALER)?shop_level:save_buffer.hero.progress.level;
   if(!target)target=1;
   unsigned old=save_buffer.other[v-1].level;
   if(v==VENDOR_PREMIUM){if(fresh)smith_stock_init(&save_buffer.other[v-1],item_seed(save_buffer.seed^0x5052454du),1);if(!premium_refresh(&save_buffer.other[v-1],&save_buffer.premium,target,fresh))return 0;continue;}
   if(fresh||(v==VENDOR_BOY?(target/2>old/2||save_buffer.other[v-1].sold):target>old))smith_stock_init(&save_buffer.other[v-1],item_seed(save_buffer.seed^0x57495443u^(v<<16)^target^(fresh?0:save_buffer.other[v-1].seed)),target);
  }
 return 1;
}
/* Construct a valid personal world without touching the active shared world. */
static UI_FN int save_personal_begin(const CharacterState *hero,uint32_t seed){
 clear_save_buffer();save_buffer.hero=*hero;save_buffer.seed=seed;save_buffer.gamma=gamma_level;save_buffer.metadata_padding=options_volume_pack(audio_music_enabled?audio_music_volume:0,audio_effects_volume);save_buffer.metadata_padding3=options_dpad_pack(dpad_rotation);save_buffer.quest=item_seed(seed^0x51524731u)%3!=0;
 if(save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);
 save_buffer.location.x=75;save_buffer.location.y=68;save_buffer.location.area=1;save_buffer.location.reserved=1;
 int x=(1536+7*16)*256,y=(128+143*8)*256;save_buffer.player=(Motion){x,y,x,y,x,y,0,0,0};
 game_quest_init(&save_buffer.quests,seed);capture_history();
 return save_prepare_shops(1)&&save_metadata_valid((const SaveMeta*)&save_buffer);
}
static UI_FN int region_prepare_metadata(const CharacterState *hero,unsigned level,int fresh){
 save_buffer.hero=*hero;save_buffer.gamma=gamma_level;save_buffer.animation=0;
 save_metadata();if(region_shortcut&&region_stage.area)save_buffer.quests.finale_flags|=(uint16_t)(1u<<(12+region_shortcut));region_player_position(&region_stage);save_location(&region_stage);save_buffer.location.reserved=level;
 unsigned destination=region_stage.area?0:level;
 if(fresh){game_quest_init(&save_buffer.quests,campaign_seed);for(unsigned i=0;i<TOWN_DROPS;i++)save_buffer.town_drops[i]=(SavedDrop){0};}
 if(destination){if(level==REGION_VILE&&save_buffer.quests.stage[15]==2)save_buffer.quests.stage[15]=3;save_buffer.generated|=1u<<(level-1);if(level<=PLAYABLE_FLOORS)save_buffer.quests.visited|=1u<<(level-1);if(level>=REGION_KING){unsigned q=level==REGION_KING?12:level==REGION_WATER?13:level==REGION_BONE?14:15;if(save_buffer.quests.state[q]==1){save_buffer.quests.state[q]=2;save_buffer.quests.stage[q]=1;}}}
 if((fresh||region_stage.area)&&!save_prepare_shops(fresh)){region_error=1;return 0;}
 return 1;
}
static UI_FN int region_commit(Game *g,unsigned level,unsigned outgoing,int fresh){
 game_get_character(g,&save_buffer.hero);
 if(!region_prepare_metadata(&save_buffer.hero,level,fresh))return 0;
 unsigned destination=region_stage.area?0:level;
 save_buffer.metadata_reserved=fresh?0:coop_campaign_epoch+1;
 if(!archive_write(outgoing,destination,fresh)){region_error=1;return 0;}
 coop_campaign_epoch=save_buffer.metadata_reserved;
 for(unsigned i=0;i<sizeof(Game);i++)((uint8_t*)g)[i]=((uint8_t*)&region_stage)[i];
 quest_progress=save_buffer.quests;town_premium=save_buffer.premium;town_smith=save_buffer.smith;for(unsigned i=0;i<VENDOR_COUNT-1;i++)town_other[i]=save_buffer.other[i];
 dungeon_level=level;if(destination)game_explore_import(save_buffer.floor.explored);else game_explore_reset();
 game_apply_character(g,&save_buffer.hero);game_quest_town(g);game_unique_restore(g);return 1;
}
int UI_FN game_region_town(Game *g,int fresh){
 if(coop_active)return !fresh&&coop_request_region(0,-1,-1,-1);
 region_error=0;audio_stop();loading_begin(fresh?6:1);for(int i=0;i<4;i++)raw_base[i]=0;
 if(fresh)clear_save_buffer();else if(!save_read()){region_error=1;return 0;}
 unsigned outgoing=!g->area&&!fresh?dungeon_level:0;
 if(outgoing){if(!floor_capture(&save_buffer.outgoing,g)){region_error=1;return 0;}save_buffer.generated|=1u<<(outgoing-1);}
 for(unsigned i=0;i<sizeof(Game);i++)((uint8_t*)&region_stage)[i]=((uint8_t*)g)[i];
 Game *s=&region_stage;s->trap_spent=0;s->area=1;s->x=fresh?75:25;s->y=fresh?68:31;
 if(region_shortcut){static const uint8_t xy[6]={49,22,18,69,41,81};s->x=xy[(region_shortcut-1)*2];s->y=xy[(region_shortcut-1)*2+1];}
 for(unsigned i=0;i<sizeof(s->map);i++)((uint8_t*)s->map)[i]=0;
 for(unsigned i=0;i<sizeof(s->props);i++)((uint8_t*)s->props)[i]=0;
 for(int i=0;i<MAX_ENEMIES;i++){s->enemies[i].alive=0;s->enemy_attack[i]=0;}
 town_drops_apply(s);
 region_reset(s);return region_commit(g,1,outgoing,fresh);
}
static UI_FN int region_destination_allowed(unsigned level){
 if(level<1||level>REGION_BONE)return 0;
 if(level>=REGION_KING&&!quest_progress.state[level==REGION_KING?12:level==REGION_WATER?13:level==REGION_BONE?14:15])return 0;
 return !((level==REGION_BONE&&quest_progress.stage[14]<1)||(level==16&&quest_progress.state[15]!=3)||(level==REGION_VILE&&quest_progress.stage[15]<2));
}
int UI_FN game_region_floor(Game *g,unsigned level){
 if(!region_destination_allowed(level))return 0;
 if(coop_active)return coop_request_region(level,-1,-1,-1);
 region_error=0;audio_stop();loading_begin(loading_destination(0,level));
 if(!save_read()){region_error=1;return 0;}
 unsigned outgoing=g->area?0:dungeon_level;
 if(outgoing){if(!floor_capture(&save_buffer.outgoing,g)){region_error=1;return 0;}save_buffer.generated|=1u<<(outgoing-1);}
 Game *s=&region_stage;floor_generate(s,level);
 if(save_buffer.generated&(1u<<(level-1))){
  if(!floor_read(level,&save_buffer.floor)){region_error=1;return 0;}
  floor_apply(s,&save_buffer.floor);
 }else{
  for(unsigned i=0;i<sizeof(save_buffer.floor);i++)((uint8_t*)&save_buffer.floor)[i]=0;
  if(!floor_capture(&save_buffer.floor,s)){region_error=1;return 0;}for(unsigned i=0;i<200;i++)save_buffer.floor.explored[i]=0;
 }
 s->area=0;
 if(region_shortcut)game_shortcut_spawn(s);
 else if(level==REGION_BONE){s->x=53;s->y=23;}
 else if(level==REGION_VILE){s->x=19;s->y=20;}
 else if(level==REGION_WATER){s->x=15;s->y=67;}
 else if(level==REGION_KING){s->x=67;s->y=29;}
 else if(outgoing==REGION_BONE&&level==6){for(unsigned i=0;i<MAX_PROPS;i++)if(s->props[i].kind==PROP_BONE_ENTRY){s->x=s->props[i].x;s->y=s->props[i].y;break;}}
 else if(outgoing==REGION_VILE&&level==15){
  for(unsigned i=0;i<MAX_PROPS;i++)if(s->props[i].kind==PROP_VILE_ENTRY){s->x=s->props[i].x;s->y=s->props[i].y;break;}
 }else if(outgoing==REGION_WATER&&level==2){
  for(unsigned i=0;i<MAX_PROPS;i++)if(s->props[i].kind==PROP_WATER_ENTRY){s->x=s->props[i].x;s->y=s->props[i].y+1;break;}
 }else if(outgoing==REGION_KING&&level==3){
  for(unsigned i=0;i<MAX_PROPS;i++)if(s->props[i].kind==PROP_KING_ENTRY){s->x=s->props[i].x+1;s->y=s->props[i].y;if(!game_walkable(s,s->x,s->y)){s->x=s->props[i].x;s->y=s->props[i].y;}break;}
 }else if(outgoing&&outgoing<=PLAYABLE_FLOORS&&level<dungeon_level){
  s->x=s->down_x;s->y=s->down_y;
  int found=0;for(int radius=0;radius<=4&&!found;radius++)for(int dy=-radius;dy<=radius&&!found;dy++)for(int dx=-radius;dx<=radius&&!found;dx++)if(game_walkable(s,s->down_x+dx,s->down_y+dy)){s->x=s->down_x+dx;s->y=s->down_y+dy;found=1;}
 }else game_dungeon_spawn(s);
 region_reset(s);return region_commit(g,level,outgoing,0);
}
int UI_FN game_region_dungeon(Game *g){return game_region_floor(g,1);}
int UI_FN game_region_shortcut(Game *g,unsigned level){
 if(level!=5&&level!=9&&level!=13)return 0;
 if(g->area&&!(game_shortcut_mask()&(1u<<((level-5)/4))))return 0;
 if(coop_active){
  if(g->area)return coop_request_region(level,-1,-1,-1);
  static const uint8_t xy[6]={49,22,18,69,41,81};unsigned i=(level-5)/4;
  return coop_request_region(0,xy[i*2],xy[i*2+1],-1);
 }
 region_shortcut=(uint8_t)(1+(level-5)/4);
 int ok=g->area?game_region_floor(g,level):game_region_town(g,0);
 region_shortcut=0;return ok;
}

static UI_FN void menu_clear(void){
 lighting_hardware_stop();
    R16(0x04000000)=0;R16(0x04000050)=0;frame_state&=~1u;
    map_last_x=-1;
    for(int i=0;i<640;i++)R16(0x0600D800+i*2)=128;
    panel_map();last_hud_tick=0xffffffffu;
}
#include "status.inc"
#include "town_dialogue.inc"
#include "death_red.inc"
static UI_FN __attribute__((target("thumb"),minsize)) void menu_render(void){
 if(menu_mode<=6||menu_mode==15){menu_canvas_draw();return;}
 if(menu_mode==11&&!game.area&&((quest_notice<42&&quest_prose71[quest_notice].title)||(quest_notice>=44&&quest_notice<53))){dialogue_session=1;dialogue_font_ready=0;dialogue_say(TOWN_DIALOGUE_MESSAGE_COUNT+quest_notice,0);}
 unsigned npc_canvas=(menu_mode>=NPC_LANDING&&menu_mode<=NPC_SPEECH)||(menu_mode>=7&&menu_mode<=10)||(dialogue_session&&menu_mode==11);
 if(!npc_canvas)dialogue_restore_bg();
 else if(hud_transparent){
  /* The live status raster and menu fonts share VRAM. Hide the old map
   * before replacing its glyphs; publish the NPC canvas only when ready.
   * Keep ordinary dialogue navigation visible to avoid selection flicker. */
  info_stream_stop();lighting_hardware_stop();frame_state&=~1u;map_pending=0;
  unsigned scan;do{scan=R16(0x04000006);}while(scan<160||scan>220);
  R16(0x04000000)=0;
 }
 dialogue_dirty=0;
 R16(0x07000000+127*8)=0x0200;R16(0x07000000+126*8)=0x0200;
 hud_font_mode(0);for(unsigned i=14;i<42;i++)R16(0x07000000+i*8)=0x0200;
 /* Contact effects such as Pepin's heal are already committed. Publish the
  * globes while the shop is open, after cancelling any pending map payload. */
 if(active_game)vital_bars();
    for(int i=0;i<14;i++)R16(0x07000000+i*8)=0x0200;
    for(int i=TOWN_MAP_NPC_OAM;i<TOWN_MAP_NPC_OAM+NPC_COUNT;i++)R16(0x07000000+i*8)=0x0200;
    if(npc_canvas){dialogue_draw();return;}
    menu_clear();
    if(active_game&&game.hp<=0){
     death_red_back();
     hud_transparent=1;unpack_font(game.area?asset_town_font:asset_dungeon_font);unpack_large_font();hud_transparent=0;
    }else for(int y=2;y<18;y++)hud_text(3,y,"                        ");
    if(menu_mode==15){
     hud_title(6,3,"DIFFICULTY");for(unsigned i=0;i<GAME_DIFFICULTY_COUNT;i++){hud_text(6,7+2*i,game_difficulty_name(i));if(menu_choice==i)hud_text(4,7+2*i,"-");}
     hud_text(3,14,"APPLIES TO THE NEW WORLD");hud_text(4,16,"A SELECT  B BACK");return;
    }
    if(menu_mode==13){
     hud_title(5,3,"DELETE GROUND ITEM");
     if(confirm_action<MAX_DROPS){Item it=item_from_drop(&game.drops[confirm_action]);char name[64],row[25];item_name(&it,name);unsigned pos=0;
      for(unsigned line=0;line<3;line++){unsigned j=0;while(j<24&&name[pos])row[j++]=name[pos++];row[j]=0;hud_text_color(3,6+line,row,item_name_color(&it,0));}}
     hud_text(3,10,"TO MAKE ROOM FOR REWARD");hud_text(3,12,"DELETION IS PERMANENT");
     hud_text(6,14,confirm_yes?"- YES       NO":"YES       - NO");hud_text(3,16,"A CONFIRM  B CANCEL");return;
    }
    if(menu_mode==14){hud_title(5,4,"REWARD IS WAITING");hud_text(3,8,"CLEAR ITEMS ON THE GROUND");hud_text(3,10,"YOUR REWARD IS KEPT");hud_text(3,14,"A/B BACK");return;}
    if(menu_mode==12){
     hud_title(7,4,"SAVE FAILED");hud_text(3,8,"CAMPAIGN AND HERO RETAINED");
     hud_text(3,10,"ENDING WAITS FOR THE SAVE");hud_text(7,15,"A RETRY SAVE");return;
    }
    if(menu_mode==11&&quest_notice>=38){hud_title(5,4,(quest_notice==39||quest_notice==41)?"WARLORD OF BLOOD":"CHAMBER OF BONE");hud_text(3,7,quest_notice==38?"THE HIDDEN STAIRS ARE OPEN":quest_notice==39?"THE WARLORDS HALL IS OPEN":quest_notice==40?"GUARDIAN SPELL INCREASED":"PREPARE TO FIGHT");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=31){hud_title(5,4,quest_notice<35?"GHARBAD THE WEAK":"ZHAR THE MAD");hud_text(3,7,quest_notice==31?"SPARE ME / RETURN LATER":quest_notice==32?"A GIFT FOR YOUR MERCY":quest_notice==33?"YOUR GIFT IS NOT READY":quest_notice==34?"NOW YOU MUST FACE ME":quest_notice==35?"TAKE THIS BOOK AND LEAVE":quest_notice==36?"YOU DISTURBED ME AGAIN":"NO ROOM FOR THE REWARD");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=25){hud_title(6,4,"BLACK MUSHROOM");hud_text(3,7,quest_notice==25?"FIND A BLACK MUSHROOM":quest_notice==26?"BRING A BRAIN TO PEPIN":quest_notice==27?"SHOW ADRIA THE ELIXIR":quest_notice==28?"THE ELIXIR IS READY":quest_notice==29?"NO ROOM FOR THE REWARD":"FIND THE FUNGAL TOME");hud_text(3,10,quest_notice==28?"USE IT FROM INVENTORY":"RETURN WHEN YOU HAVE IT");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=21){hud_title(6,4,"THE BETRAYER");hud_text(3,7,quest_notice==21?"THE STAFF BELONGED TO HIM":quest_notice==22?"THE PENTAGRAM IS OPEN":quest_notice==24?"YOU ARE TOO LATE":"BRING HIS STAFF TO CAIN");hud_text(3,9,quest_notice==21?"ENTER THE RED PORTAL":quest_notice==22?"DIABLO WAITS ON LEVEL 16":quest_notice==24?"DEFEAT LAZARUS":"SEARCH LEVEL 15");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=18){hud_title(7,4,"POISONED WATER");hud_text(3,7,quest_notice==18?"FIND THE CAVE ON LEVEL 2":quest_notice==19?"THE WATER IS CLEAN AGAIN":"NO ROOM FOR THE REWARD");hud_text(3,9,quest_notice==18?"DEFEAT ALL THE MONSTERS":quest_notice==19?"YOUR RING IS ON THE GROUND":"RETURN TO PEPIN");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=16){hud_title(6,4,"KING LEORIC");hud_text(3,7,quest_notice==16?"FIND HIS LAIR ON LEVEL 3":"THE SKELETON KING IS DEAD");hud_text(3,9,quest_notice==16?"FREE HIM FROM THE CURSE":"THANK YOU FOR YOUR HELP");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=11){hud_title(7,4,"OGDENS SIGN");hud_text(3,7,quest_notice==11?"FIND MY SIGN ON LEVEL 4":quest_notice==12?"THANK YOU FOR MY SIGN":quest_notice==13?"NO ROOM FOR THE REWARD":quest_notice==14?"BRING ME THE TAVERN SIGN":"YOU HAVE BEEN TRICKED");hud_text(3,9,quest_notice==12?"YOUR REWARD IS ON THE GROUND":quest_notice==14?"THE SIGN CHEST IS UNLOCKED":" ");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice>=8){hud_title(4,4,quest_notice==8?"HALLS OF THE BLIND":"LACHDANAN");hud_text(3,7,quest_notice==8?"THE HIDDEN HALLS ARE OPEN":quest_notice==9?"FIND THE GOLDEN ELIXIR":"MY SOUL IS FREE AT LAST");hud_text(3,9,quest_notice==8?"FIND THE OPTIC AMULET":quest_notice==9?"SEARCH LEVEL 15":"TAKE THE VEIL OF STEEL");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11&&quest_notice==7){hud_title(11,4,"VALOR");hud_text(3,7,"FIND THREE BLOOD STONES");hud_text(3,9,"PLACE THEM IN THE PEDESTAL");hud_text(3,11,"CLAIM ARKAINES VALOR");hud_text(4,14,"A/B BACK");return;}
    if(menu_mode==11){unsigned stage=(quest_notice-1)%3+1;int anvil=quest_notice>3;hud_title(6,4,anvil?"ANVIL OF FURY":"THE MAGIC ROCK");hud_text(3,7,stage==1?(anvil?"FIND THE ANVIL ON LEVEL 10":"FIND THE ROCK ON LEVEL 5"):stage==2?(anvil?"THANK YOU FOR THE ANVIL":"THANK YOU FOR THE ROCK"):"NO ROOM FOR THE REWARD");hud_text(3,9,stage==2?"YOUR REWARD IS ON THE GROUND":"RETURN TO GRISWOLD");hud_text(4,14,"A/B BACK");return;}

}
static UI_FN void menu_draw(void){
 menu_render();
 if(menu_mode>=11&&menu_mode<=14&&!dialogue_session){
  while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
  R16(0x0400000a)=0x1b88;R16(0x04000000)=0x0240;
 }
}
static UI_FN void menu_close(void){
    dialogue_end();
    inventory_release();
    if(active_game&&game.hp<=0){menu_mode=1;menu_choice=coop_active?2:3;menu_draw();return;}
    menu_mode=active_game?0:5;menu_clear();
    /* Loading gameplay fonts also restores a static LEVEL strip. Do that
     * before drawing the live region name, never on the following frame. */
    if(active_game){hud_gameplay();panel_map();hud();}else menu_draw();
}
static UI_FN void menu_reload(unsigned animation){
    load_scene();reset_visuals();draw(animation);reveal_scene();
}
static UI_FN __attribute__((target("thumb"),minsize)) void menu_input(unsigned pressed,unsigned *animation){
    audio_speech_poll();
    if(menu_mode==NPC_SPEECH)pressed=dialogue_repeat(pressed);
    if(!pressed)return;
    if(menu_mode>=NPC_LANDING&&menu_mode<=NPC_SPEECH){dialogue_input(pressed);return;}
    if(menu_mode==15){
     if(pressed&(KEY_B|KEY_START)){menu_mode=1;menu_choice=2;}
     else{if(pressed&KEY_UP)menu_choice=(menu_choice+GAME_DIFFICULTY_COUNT-1)%GAME_DIFFICULTY_COUNT;if(pressed&KEY_DOWN)menu_choice=(menu_choice+1)%GAME_DIFFICULTY_COUNT;
      if(pressed&KEY_A){new_difficulty=menu_choice;confirm_action=2;confirm_yes=0;menu_mode=3;}}menu_draw();return;
    }
    if(menu_mode==14){if(pressed&(KEY_A|KEY_B|KEY_START))menu_close();return;}
    if(menu_mode==13){
     if(pressed&(KEY_B|KEY_START)){game_reward_resolve(&game,confirm_action,0);menu_mode=14;menu_draw();return;}
     if(pressed&(KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN))confirm_yes^=1;
     if(pressed&KEY_A){if(game_reward_resolve(&game,confirm_action,confirm_yes))menu_close();else menu_mode=14;}
     if(menu_mode)menu_draw();return;
    }
    if(menu_mode==12)return; /* The ending flow owns save retry input. */
    if(menu_mode==11){if(pressed&(KEY_A|KEY_B|KEY_START)){if(dialogue_session&&!(pressed&KEY_START)){menu_mode=dialogue_return;menu_draw();}else menu_close();}return;}
    if(menu_mode>=7){if(shop_input(pressed))menu_close();else menu_draw();return;}
    if(menu_mode==5){if(pressed&(KEY_START|KEY_A)){menu_mode=1;menu_choice=2;menu_draw();}return;}
    if(pressed&KEY_START){menu_close();return;}
    if(pressed&KEY_B){
        if(menu_mode==1||menu_mode==6)menu_close();
        else{menu_mode=1;menu_choice=coop_active&&game.hp<=0?1:0;menu_draw();}return;
    }
    if(menu_mode==1&&(pressed&KEY_R)&&active_game){menu_mode=6;menu_choice=0;menu_draw();return;}
    if(menu_mode==6){
        if(pressed&KEY_UP)menu_choice=(menu_choice+3)%4;
        else if(pressed&KEY_DOWN)menu_choice=(menu_choice+1)%4;
        if(pressed&KEY_A)game_spend_stat(&game,menu_choice);
        menu_draw();return;
    }
    if(menu_mode==4){if(pressed&KEY_A){menu_mode=1;menu_choice=0;menu_draw();}return;}
    if(menu_mode==3){
        confirm_yes=1;
        if(pressed&KEY_A){
            if(!confirm_yes){menu_mode=1;menu_choice=confirm_action;}
            else if(confirm_action==4){
                active_game=0;menu_mode=5;R16(0x04000000)=0x0240;
            }else if(confirm_action==2){
                CharacterState hero;
                if(session_hero)game_get_character(&game,&hero);
                else if(save_read()){hero=save_buffer.hero;gamma_level=save_buffer.gamma;options_volume_apply(save_buffer.metadata_padding);dpad_rotation=options_dpad_unpack(save_buffer.metadata_padding3);}
                else{menu_mode=4;menu_notice="NO VALID HERO";menu_draw();return;}
                game_seed_next((uint32_t)R16(0x0400010C) ^ graphics_frames*2654435761u ^ (uint32_t)game.ticks*2246822519u);
                game_set_difficulty(new_difficulty);game_new_campaign(&game,&hero);active_game=session_hero=1;
                *animation=0;menu_reload(*animation);
                if(region_error){region_error=0;active_game=session_hero=0;menu_mode=4;menu_notice="NEW MAP NOT SAVED";menu_draw();return;}
                menu_close();return;
            }else if(save_read()){
                if(!save_restore(&game)){menu_reload(*animation);menu_mode=4;menu_notice="LOAD FAILED";menu_draw();return;}
                gamma_level=save_buffer.gamma;*animation=save_buffer.animation;active_game=session_hero=1;
                Motion player=save_buffer.player;
                load_scene();reset_visuals();motion[0]=player;
                draw(*animation);reveal_scene();menu_close();return;
            }else{menu_mode=4;menu_notice="NO VALID SAVE";}
        }
        menu_draw();return;
    }
    if(menu_mode==1&&active_game&&game.hp<=0&&!menu_choice&&(pressed&KEY_A)){menu_draw();return;}
    unsigned item_count=5;
    if(pressed&KEY_UP)menu_choice=(menu_choice+item_count-1)%item_count;
    else if(pressed&KEY_DOWN)menu_choice=(menu_choice+1)%item_count;
    if(menu_mode==1&&active_game&&game.hp<=0&&!menu_choice)menu_choice=(pressed&KEY_UP)?4:1;
    if(menu_mode==2){
        if(coop_active&&!menu_choice)menu_choice=(pressed&KEY_UP)?4:1;
        if(menu_choice==0 && (pressed&(KEY_LEFT|KEY_RIGHT))){
            audio_music_enabled=1;
            audio_set_music_volume((unsigned)clamp((int)audio_music_volume+((pressed&KEY_RIGHT)?1:-1),1,10));
        }
        if(menu_choice==1 && (pressed&(KEY_LEFT|KEY_RIGHT))){
            audio_effects_enabled=1;
            audio_set_effects_volume((unsigned)clamp((int)audio_effects_volume+((pressed&KEY_RIGHT)?1:-1),1,10));
        }
        if(menu_choice==2 && (pressed&(KEY_LEFT|KEY_RIGHT))){
            gamma_level=(unsigned)clamp((int)gamma_level+((pressed&KEY_RIGHT)?1:-1),0,OPTIONS_GAMMA_LEVELS-1);apply_gamma();
        }
        if(menu_choice==3&&(pressed&(KEY_LEFT|KEY_RIGHT|KEY_A))){
            /* Visible order: CCW, Original, CW; A advances like Right. */
            dpad_rotation=(dpad_rotation+((pressed&KEY_LEFT)?1:2))%OPTIONS_DPAD_COUNT;
        }
        if((pressed&KEY_A)&&menu_choice==4){menu_mode=1;menu_choice=coop_active&&game.hp>0?0:1;}
    }else if(pressed&KEY_A){
        if(menu_choice==0){
            menu_mode=4;menu_notice=!active_game||game.hp<=0?"SAVE UNAVAILABLE":save_write(*animation)?"GAME SAVED":"SAVE FAILED";
        }else if(menu_choice==1){menu_mode=2;menu_choice=0;}
        else if(menu_choice==2){menu_mode=15;menu_choice=game_difficulty;}
        else if(menu_choice==3 && !save_read()){menu_mode=4;menu_notice="NO VALID SAVE";}
        else{confirm_action=menu_choice;confirm_yes=0;menu_mode=3;}
    }
    menu_draw();
}

