/* Host-owned campaign transactions. Scratch dies before scene artwork is loaded.
 * Flash is atomic on the host cartridge; a lost COMMIT requires rejoining that
 * host's durable epoch. There is deliberately no cross-cartridge rollback. */
#include "coop_campaign.h"
#include "generation_context.h"
#include "world_rooms.h"
#include "spells.h"
#include "link_diag.h"

typedef struct {
 CoopPlayer players[4];
 GameGenerationState old_generation,next_generation;
 SaveMeta old_binding;
 uint8_t *old_workspace;
 unsigned old_active,old_campaign,old_turn;
 uint8_t town_mask[1152];
 uint32_t header[8];
 /* Transaction-owned copy: town_map is reused by loading artwork. */
 uint8_t macros[1600];
 uint32_t macro_seed,macro_key,macro_hash;
} CampaignScratch;
#define CAMPAIGN_SCRATCH_OFFSET ((sizeof(SavePayload)+7u)&~7u)
#define campaign_scratch (*(CampaignScratch*)((uint8_t*)load_buffer+CAMPAIGN_SCRATCH_OFFSET))
_Static_assert(CAMPAIGN_SCRATCH_OFFSET+sizeof(CampaignScratch)<=sizeof(load_buffer),"campaign staging capacity");
enum { CAMPAIGN_RESET=1,CAMPAIGN_VISITED=2,CAMPAIGN_ENTRY=4,CAMPAIGN_MAGIC=0x43414d30u };
static unsigned campaign_initializing;
static __attribute__((minsize)) int UI_FN coop_campaign_can_initialize(void){return campaign_initializing||flash_save_empty();}

static __attribute__((minsize)) UI_FN void campaign_copy(void *to,const void *from,unsigned bytes){uint8_t *d=to;const uint8_t *s=from;for(unsigned i=0;i<bytes;i++)d[i]=s[i];}
static __attribute__((minsize)) UI_FN void campaign_zero(void *to,unsigned bytes){uint8_t *d=to;for(unsigned i=0;i<bytes;i++)d[i]=0;}
static __attribute__((minsize)) UI_FN uint32_t campaign_crc(uint32_t crc,const void *data,unsigned bytes){
 const uint8_t *p=data;for(unsigned i=0;i<bytes;i++){crc^=p[i];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}return crc;
}
static __attribute__((minsize)) UI_FN int campaign_vote(unsigned ready,int commit){
 uint16_t values[4];int ok=commit?coop_wire_commit_frame((uint16_t)ready,values,0):coop_wire_frame((uint16_t)ready,values,0);
 if(!ok)return 0;for(unsigned i=0;i<4;i++)if((coop_wire_members()&(1u<<i))&&values[i]!=1)return link_diag_fault(LE_PEER_REJECT,i+1,values[i]);return 1;
}
static __attribute__((minsize)) UI_FN int campaign_transfer(void *data,unsigned bytes){
 uint8_t *p=data;for(unsigned i=0;i<bytes;i+=256){
  unsigned n=bytes-i;if(n>256)n=256;
  link_diag_position(i,bytes);if(!coop_wire_block(p+i,n))return 0;
  if(loading_screen_active){
   unsigned base=link_diag.stage==LD_HEADER?10:link_diag.stage==LD_METADATA?12:28;
   unsigned span=link_diag.stage==LD_HEADER?2:link_diag.stage==LD_METADATA?16:16;
   unsigned percent=base+span*(i+n)/bytes;
   if(percent>=loading_phase+4||i+n==bytes)loading_step(percent,0);
  }
 }return 1;
}
static __attribute__((minsize)) UI_FN int campaign_agree(uint32_t value){
 uint16_t values[4];for(unsigned b=0;b<4;b++){
  link_diag_position(b,4);unsigned byte=(value>>(b*8))&255;if(!coop_wire_frame(byte,values,0))return 0;
  for(unsigned i=0;i<4;i++)if((coop_wire_members()&(1u<<i))&&values[i]!=byte)return link_diag_fault(LE_AGREEMENT,byte,values[i]);
 }return 1;
}
static __attribute__((minsize)) UI_FN int campaign_hero_valid(const CharacterState *h,int dead){
 return (dead||h->hp>0)&&h->mana>=0&&h->max_mana>=h->mana&&h->gold>=0&&
 (h->reserved&0xff00)==0x4300&&(h->reserved&255)<=2&&game_belt_valid(h->belt,h->potions)&&game_progress_valid(h)&&items_valid(&h->inventory);
}
static __attribute__((minsize)) UI_FN void campaign_hero_effects(CoopPlayer *p){
 ItemEffects e;item_build_effects(&p->hero.progress,&p->hero.inventory,p->hero.reserved&255,&e);
 int lo=e.weapon_low,hi=e.weapon_high;lo+=lo*e.damage_percent/100+e.damage_flat;hi+=hi*e.damage_percent/100+e.damage_flat;
 p->damage_min=lo>0?(lo<256?lo:255):1;p->damage_max=hi>p->damage_min?(hi<256?hi:255):p->damage_min;
 p->armor=e.armor<0?0:e.armor>255?255:e.armor;p->weapon_flags=e.weapon_flags;
 p->hero.max_mana=e.max_mana;if(e.special_flags&ITEM_NOMANA){p->hero.mana=0;p->hero.progress.mana_fraction=0;}
}
static __attribute__((minsize)) UI_FN void campaign_capture_binding(void){
 CampaignScratch *c=&campaign_scratch;SaveMeta *m=&c->old_binding;
 m->seed=campaign_seed;m->quest=butcher_quest;m->quests=quest_progress;m->metadata_padding2=game_difficulty;m->metadata_reserved=coop_campaign_epoch;m->location.reserved=dungeon_level;
 m->smith=town_smith;m->premium=town_premium;for(unsigned i=0;i<VENDOR_COUNT-1;i++)m->other[i]=town_other[i];
 c->old_active=coop_active;c->old_campaign=coop_campaign_active;c->old_turn=coop_turn;c->old_workspace=dungeon_workspace;
 game_generation_capture(&c->old_generation);
}
static __attribute__((minsize)) UI_FN void campaign_bind(const SaveMeta *m){
 campaign_seed=m->seed;butcher_quest=m->quest;quest_progress=m->quests;game_set_difficulty(m->metadata_padding2);dungeon_level=m->location.reserved;
 town_smith=m->smith;town_premium=m->premium;for(unsigned i=0;i<VENDOR_COUNT-1;i++)town_other[i]=m->other[i];
}
static __attribute__((minsize)) UI_FN void campaign_restore_binding(void){
 CampaignScratch *c=&campaign_scratch;campaign_bind(&c->old_binding);coop_campaign_epoch=c->old_binding.metadata_reserved;
 coop_active=c->old_active;coop_campaign_active=c->old_campaign;coop_turn=c->old_turn;dungeon_workspace=c->old_workspace;game_generation_apply(&c->old_generation);
}
static __attribute__((minsize)) UI_FN int campaign_floor_valid(const FloorState *f){
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const SavedEnemy *e=&f->enemies[i];unsigned position=e->position[0]|((unsigned)e->position[1]<<8),alive=position>>14;
  if((position&127)>=80||((position>>7)&127)>=80||e->kind>=MONSTER_TYPES||alive>2)return 0;
  unsigned maximum=game_monster_hp_unarchive(e->kind,e->maximum);int hp=(int16_t)(e->hp[0]|((unsigned)e->hp[1]<<8));
  if(e->maximum!=255&&!maximum)return 0;
  if(alive&&(hp<=0||(maximum&&(unsigned)hp>maximum)))return 0;
 }
 for(unsigned i=0;i<MAX_DROPS;i++){const uint8_t *p=f->drops[i].position;unsigned bits=p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16);if((bits&127)>=80||((bits>>7)&127)>=80||(bits>>23)||((bits&(1u<<14))&&((bits>>15)&255)>=ITEM_COUNT))return 0;}
 for(unsigned i=0;i<MAX_PROPS;i++){const uint8_t *p=f->props[i].data;unsigned bits=p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16);Prop prop=game_prop_archive_unpack(bits);if(game_prop_archive_pack(&prop)!=bits)return 0;}
 return 1;
}
static __attribute__((minsize)) UI_FN int campaign_walkable(int x,int y){
 if(region_stage.area){if(x<0||y<0||x>=96||y>=96)return 0;unsigned n=y*96+x;return (campaign_scratch.town_mask[n>>3]>>(n&7))&1;}
 if(!game_walkable(&region_stage,x,y))return 0;
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(region_stage.enemies[i].alive&&region_stage.enemies[i].x==x&&region_stage.enemies[i].y==y)return 0;
 return 1;
}
static __attribute__((minsize)) UI_FN void campaign_arrival(unsigned destination,unsigned source,int x,int y){
 Game *s=&region_stage;
 if(x>=0&&y>=0){s->x=x;s->y=y;return;}
 if(!destination){s->x=25;s->y=31;return;}
 if(!source&&(destination==5||destination==9||destination==13)){game_shortcut_spawn(s);return;}
 if(destination>=REGION_KING){static const uint8_t fixed[8]={67,29,15,67,19,20,53,23};s->x=fixed[(destination-REGION_KING)*2];s->y=fixed[(destination-REGION_KING)*2+1];return;}
 unsigned kind=source==REGION_BONE?PROP_BONE_ENTRY:source==REGION_VILE?PROP_VILE_ENTRY:source==REGION_WATER?PROP_WATER_ENTRY:source==REGION_KING?PROP_KING_ENTRY:0;
 if(kind){for(unsigned i=0;i<MAX_PROPS;i++)if(s->props[i].kind==kind){s->x=s->props[i].x+(source==REGION_KING);s->y=s->props[i].y+(source==REGION_WATER);return;}}
 if(source&&source<=PLAYABLE_FLOORS&&destination<source){s->x=s->down_x;s->y=s->down_y;}else game_dungeon_spawn(s);
}
static __attribute__((minsize)) UI_FN int campaign_stage(void){
 CampaignScratch *c=&campaign_scratch;unsigned dest=c->header[1],source=c->header[2],flags=c->header[3];
 campaign_bind((const SaveMeta*)&save_buffer);coop_active=coop_campaign_active=1;coop_turn=0;dungeon_workspace=(uint8_t*)town_map;
 if(dest){
  if(!region_destination_allowed(dest)){campaign_restore_binding();return 0;}
  /* Only caches authored by this floor may enter the shared candidate. Other
   * quest caches belong to each peer's previous private region. */
  campaign_zero(&c->next_generation,sizeof(c->next_generation));
  c->next_generation.room_live=c->old_generation.room_live;c->next_generation.butcher_room[0]=c->next_generation.butcher_room[1]=-1;
  game_generation_apply(&c->next_generation);dungeon_level=dest;floor_generate(&region_stage,dest);
  c->macro_key=0;
  if(dest>=5&&dest<=16){
   campaign_copy(c->macros,town_map,sizeof(c->macros));
   c->macro_seed=game_floor_seed(campaign_seed,dest);c->macro_key=cave_key(dest);c->macro_hash=cave_macro_hash();
  }
  if(flags&CAMPAIGN_VISITED){if(!campaign_floor_valid(&save_buffer.floor)){campaign_restore_binding();return 0;}floor_apply(&region_stage,&save_buffer.floor);}
  else{if(!floor_capture(&save_buffer.floor,&region_stage)){campaign_restore_binding();return 0;}campaign_zero(save_buffer.floor.explored,sizeof(save_buffer.floor.explored));}
 }else{
  campaign_zero(&region_stage,sizeof(region_stage));region_stage.area=1;town_drops_apply(&region_stage);
  if(lz_decode(asset_town_collision,town_map,sizeof(town_map))!=9216){campaign_restore_binding();return 0;}
  const uint8_t *raw=(const uint8_t*)town_map;
  for(unsigned i=0;i<1152;i++){unsigned bits=0;for(unsigned b=0;b<8;b++)if(raw[i*8+b])bits|=1u<<b;c->town_mask[i]=bits;}
  unsigned wound=32*96+24;c->town_mask[wound>>3]=(c->town_mask[wound>>3]&~(1u<<(wound&7)))|((!game_wounded_present())<<(wound&7));
 }
 region_stage.area=!dest;region_reset(&region_stage);
 if(flags&CAMPAIGN_ENTRY){region_stage.x=save_buffer.location.x;region_stage.y=save_buffer.location.y;region_stage.facing=save_buffer.location.facing;}
 else campaign_arrival(dest,source,(int32_t)c->header[5],(int32_t)c->header[6]);
 int ox=region_stage.x,oy=region_stage.y;
 for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){
  CoopPlayer *p=&c->players[i];CharacterState hero=p->hero;unsigned entering=flags&CAMPAIGN_ENTRY,facing=entering?save_buffer.location.facing:p->facing,previous=entering?0:p->previous;int kills=entering?0:p->kills;campaign_zero(p,sizeof(*p));p->hero=hero;p->facing=facing;p->previous=previous;p->kills=kills;p->target=p->swing=-1;p->x=p->y=-1;
  int found=0;for(int r=0;r<12&&!found;r++)for(int dy=-r;dy<=r&&!found;dy++)for(int dx=-r;dx<=r;dx++){
   int x=ox+dx,y=oy+dy;if(!campaign_walkable(x,y))continue;unsigned j=0;for(;j<i;j++)if((coop_members&(1u<<j))&&c->players[j].x==x&&c->players[j].y==y)break;if(j<i)continue;
   p->x=x;p->y=y;found=1;break;
  }
  if(!found){campaign_restore_binding();return 0;}campaign_hero_effects(p);
 }
 region_stage.x=c->players[0].x;region_stage.y=c->players[0].y;save_buffer.hero=c->players[0].hero;
 if(save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);
 save_buffer.location.x=region_stage.x;save_buffer.location.y=region_stage.y;save_buffer.location.area=!dest;save_buffer.location.facing=region_stage.facing;save_buffer.location.reserved=dest?dest:1;region_player_position(&region_stage);
 if(dest){save_buffer.generated|=1u<<(dest-1);if(dest<=PLAYABLE_FLOORS)save_buffer.quests.visited|=1u<<(dest-1);if(dest>=REGION_KING){unsigned q=dest==REGION_KING?12:dest==REGION_WATER?13:dest==REGION_BONE?14:15;if(save_buffer.quests.state[q]==1){save_buffer.quests.state[q]=2;save_buffer.quests.stage[q]=1;}}if(dest==REGION_VILE&&save_buffer.quests.stage[15]==2)save_buffer.quests.stage[15]=3;}
 else if(!save_prepare_shops(0)){campaign_restore_binding();return 0;}
 game_generation_capture(&c->next_generation);campaign_restore_binding();return save_metadata_valid((const SaveMeta*)&save_buffer);
}
static __attribute__((minsize)) UI_FN uint32_t campaign_candidate_crc(void){
 uint32_t crc=campaign_crc(~0u,&save_buffer,sizeof(SaveMeta));crc=campaign_crc(crc,&region_stage,sizeof(region_stage));crc=campaign_crc(crc,campaign_scratch.players,sizeof(campaign_scratch.players));
 if(region_stage.area)return campaign_crc(crc,campaign_scratch.town_mask,1152);
 /* Pointer values differ between peers; the native context bytes do not. */
 const GameGenerationState *s=&campaign_scratch.next_generation;crc=campaign_crc(crc,s->quest_cells,sizeof(*s)-offsetof(GameGenerationState,quest_cells));
 game_generation_apply(s);for(int y=0;y<80;y++)for(int x=0;x<80;x++){int n=game_room_candidate_at(x,y);uint8_t v=n<0?255:(uint8_t)n;crc=campaign_crc(crc,&v,1);}game_generation_apply(&campaign_scratch.old_generation);return crc;
}
static __attribute__((minsize)) UI_FN void campaign_apply(void){
 CampaignScratch *c=&campaign_scratch;campaign_bind((const SaveMeta*)&save_buffer);coop_campaign_epoch=save_buffer.metadata_reserved;coop_active=coop_campaign_active=1;
 campaign_copy(&game,&region_stage,sizeof(game));campaign_copy(coop_players,c->players,sizeof(coop_players));game_generation_apply(&c->next_generation);
 if(game.area){campaign_copy(town_collision,c->town_mask,1152);game_rooms_invalidate();game_explore_reset();}else{game_rooms_publish();game_explore_import(save_buffer.floor.explored);}
 if(c->header[3]&CAMPAIGN_ENTRY){game_spell_new_campaign();coop_cpu_reset();coop_ticks=0;}
 game_arrows_reset();game_ai_reset();game_finale_restore(&game);coop_region_clear();
 coop_select(&game,0);game_quest_town(&game);coop_capture(&game,0);game_unique_restore(&game);game_activate_nearby(&game);coop_select(&game,coop_id);coop_checksum(&game);
}
static __attribute__((minsize)) UI_FN int campaign_transaction(unsigned destination,int x,int y,int entry){
 CampaignScratch *c=&campaign_scratch;campaign_capture_binding();
 link_diag_stage(LD_HOST_READ);
 unsigned ready=1;
 if(!coop_id){
  unsigned reset=0;if(!save_read()){if(!entry||!coop_campaign_can_initialize()||!save_personal_begin(&c->players[0].hero,0x83000001u))ready=0;else reset=1;}
  if(!ready){link_diag_fault(LE_HOST_READ,1,0);campaign_vote(0,0);return 0;}
  unsigned source=entry?(save_buffer.location.area?0:save_buffer.location.reserved):(game.area?0:dungeon_level);
  if(entry)destination=source;
  if(ready&&!entry){save_metadata();if(source&&!floor_capture(&save_buffer.outgoing,&game))ready=0;}
  unsigned visited=destination&&(save_buffer.generated&(1u<<(destination-1)));
  if(ready&&visited){if(destination==source&&!entry)campaign_copy(&save_buffer.floor,&save_buffer.outgoing,sizeof(FloorState));else if(!floor_read(destination,&save_buffer.floor)){ready=0;link_diag_fault(LE_FLOOR_READ,destination,0);}}
  if(!visited)campaign_zero(&save_buffer.floor,sizeof(FloorState));
  save_buffer.hero=c->players[0].hero;if(save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);
  save_buffer.metadata_reserved=(entry?save_buffer.metadata_reserved:coop_campaign_epoch)+1;
  c->header[0]=CAMPAIGN_MAGIC;c->header[1]=destination;c->header[2]=source;c->header[3]=(reset?CAMPAIGN_RESET:0)|(visited?CAMPAIGN_VISITED:0)|(entry?CAMPAIGN_ENTRY:0);c->header[4]=coop_members;c->header[5]=(uint32_t)x;c->header[6]=(uint32_t)y;
  uint32_t crc=campaign_crc(~0u,c->header,7*sizeof(uint32_t));crc=campaign_crc(crc,&save_buffer,sizeof(SaveMeta));c->header[7]=campaign_crc(crc,&save_buffer.floor,sizeof(FloorState));
 }
 link_diag_stage(LD_HOST_READY);if(!campaign_vote(ready,0))return 0;
 link_diag_stage(LD_HEADER);if(!campaign_transfer(c->header,sizeof(c->header)))return 0;
 link_diag_stage(LD_METADATA);if(!campaign_transfer(&save_buffer,sizeof(SaveMeta)))return 0;
 link_diag_stage(LD_FLOOR);
 if(c->header[3]&CAMPAIGN_VISITED){if(!campaign_transfer(&save_buffer.floor,sizeof(FloorState)))return 0;}
 else campaign_zero(&save_buffer.floor,sizeof(FloorState));
 link_diag_stage(LD_VALIDATE);
 unsigned flags=c->header[3];ready=c->header[0]==CAMPAIGN_MAGIC&&c->header[1]<=SAVE_FLOORS&&c->header[2]<=SAVE_FLOORS&&c->header[4]==coop_members&&!(flags&~7u)&&!!(flags&CAMPAIGN_ENTRY)==!!entry;
 int ax=(int32_t)c->header[5],ay=(int32_t)c->header[6],limit=c->header[1]?80:96;
 ready=ready&&((ax==-1&&ay==-1)||(ax>=0&&ay>=0&&ax<limit&&ay<limit));
 ready=ready&&!!(flags&CAMPAIGN_VISITED)==!!(c->header[1]&&(save_buffer.generated&(1u<<(c->header[1]-1))));
 ready=ready&&save_metadata_valid((const SaveMeta*)&save_buffer)&&(!entry?save_buffer.seed==campaign_seed&&save_buffer.metadata_reserved==coop_campaign_epoch+1:1);
 uint32_t crc=campaign_crc(~0u,c->header,7*sizeof(uint32_t));crc=campaign_crc(crc,&save_buffer,sizeof(SaveMeta));ready=ready&&campaign_crc(crc,&save_buffer.floor,sizeof(FloorState))==c->header[7];
 if(!ready)link_diag_fault(LE_METADATA,1,0);if(!campaign_vote(ready,0))return 0;
 if(loading_screen_active)loading_step(45,0);
 link_diag_stage(LD_GENERATE);ready=campaign_stage();if(!ready)link_diag_fault(LE_GENERATION,1,0);
 if(loading_screen_active)loading_step(65,0);
 link_diag_stage(LD_GENERATE_READY);if(!campaign_vote(ready,0))return 0;
 link_diag_stage(LD_STATE_CHECK);if(!campaign_agree(campaign_candidate_crc()))return 0;
 link_diag_stage(LD_COMMIT_READY);if(!campaign_vote(1,0))return 0;
 unsigned committed=0;
 if(!coop_id){link_diag_stage(LD_HOST_WRITE);if(flags&CAMPAIGN_RESET)campaign_initializing=1;committed=archive_write(entry?0:c->header[2],c->header[1],!!(flags&CAMPAIGN_RESET));if(committed)campaign_initializing=0;else link_diag_fault(LE_HOST_WRITE,1,0);}
 link_diag_stage(LD_COMMIT);
 uint16_t values[4];int received=coop_wire_commit_frame(coop_id?1:committed,values,0);
 if(!received){if(!coop_id&&committed){campaign_apply();return 2;}return 0;}
 if(!coop_id&&!committed)return 0;
 if(values[0]!=1){link_diag_fault(LE_COMMIT_REJECT,1,values[0]);if(!coop_id&&committed){campaign_apply();return 2;}return 0;}
 /* The epoch's complete32 bits, not the wire's12-bit hash, identify COMMIT. */
 link_diag_stage(LD_EPOCH);if(!campaign_agree(save_buffer.metadata_reserved)){if(!coop_id&&committed){campaign_apply();return 2;}return 0;}
 link_diag_stage(LD_APPLY);campaign_apply();return 1;
}
static UI_FN void coop_campaign_prepare_scene(void){
 CampaignScratch *c=&campaign_scratch;
 cave_handoff_key=0;
 if(!game.area&&dungeon_level>=5&&dungeon_level<=16&&c->header[1]==dungeon_level&&
    c->next_generation.room_candidate&&c->macro_key==cave_key(dungeon_level)&&c->macro_seed==game_floor_seed(campaign_seed,dungeon_level)){
  campaign_copy(town_map,c->macros,sizeof(c->macros));
  if(cave_macro_hash()==c->macro_hash){cave_handoff_key=c->macro_key;cave_handoff_seed=c->macro_seed;cave_handoff_hash=c->macro_hash;}
 }
}
static __attribute__((minsize)) int UI_FN coop_campaign_enter(const CharacterState *local){
 CampaignScratch *c=&campaign_scratch;campaign_zero(c,sizeof(*c));uint16_t values[4];
 link_diag_stage(LD_LOCAL_HERO);unsigned local_valid=campaign_hero_valid(local,0);if(!local_valid)link_diag_fault(LE_HERO_INVALID,1,0);
 if(!campaign_vote(local_valid,0))return 0;
 link_diag_stage(LD_HERO_BYTES);
 for(unsigned b=0;b<sizeof(CharacterState);b++){
  link_diag_position(b,sizeof(CharacterState));
  if(!coop_wire_frame(((const uint8_t*)local)[b],values,0))return 0;
  for(unsigned i=0;i<4;i++)if(coop_members&(1u<<i)){if(values[i]>255)return link_diag_fault(LE_BAD_BYTE,255,values[i]);((uint8_t*)&c->players[i].hero)[b]=(uint8_t)values[i];}
 }
 uint32_t cpu_seed=campaign_seed^((uint32_t)R16(0x04000100)<<8);
 if(coop_cpu_mask){link_diag_stage(LD_CPU_SEED);if(!campaign_transfer(&cpu_seed,sizeof(cpu_seed)))return 0;}
 unsigned cpu_class=0;
 for(unsigned i=1;i<4;i++)if(coop_cpu_mask&(1u<<i)){unsigned cls=cpu_class++;CharacterState *h=&c->players[i].hero;game_create_hero(h,cls);
  unsigned level=c->players[0].hero.progress.level;h->progress.level=level;h->progress.experience=game_next_experience(level-1);h->progress.points=5*(level-1);
  h->progress.max_hp_q6+=(cls==2?64:128)*(level-1);h->progress.max_mana_q6+=(cls?128:64)*(level-1);
  coop_cpu_equip(h,cpu_seed^i*0x9e3779b9u);
  ItemEffects e;item_build_effects(&h->progress,&h->inventory,cls,&e);h->hp=e.max_hp;h->mana=h->max_mana=e.max_mana;h->gold=0;}
 link_diag_stage(LD_HERO_CHECK);unsigned valid=1;for(unsigned i=0;i<4;i++)if((coop_members&(1u<<i))&&!campaign_hero_valid(&c->players[i].hero,0)){valid=0;link_diag_fault(LE_HERO_INVALID,i+1,0);}
 if(!campaign_vote(valid,0))return 0;return campaign_transaction(0,-1,-1,1);
}
static __attribute__((minsize)) int UI_FN coop_campaign_travel(unsigned destination,int x,int y){
 if(!coop_active||!coop_campaign_active||destination>SAVE_FLOORS||(destination&&!region_destination_allowed(destination)))return 0;
 CampaignScratch *c=&campaign_scratch;campaign_zero(c,sizeof(*c));campaign_copy(c->players,coop_players,sizeof(coop_players));return campaign_transaction(destination,x,y,0);
}
static __attribute__((minsize)) int UI_FN coop_campaign_host_save(void){
 if(coop_id||!coop_active||!coop_campaign_active||!save_read())return 0;
 unsigned floor=game.area?0:dungeon_level;if(floor&&!floor_capture(&save_buffer.floor,&game))return 0;
 save_metadata();save_buffer.hero=coop_players[0].hero;if(save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);
 save_location(&game);save_buffer.location.x=coop_players[0].x;save_buffer.location.y=coop_players[0].y;save_buffer.location.facing=coop_players[0].facing;
 save_player_position(save_buffer.location.x,save_buffer.location.y,save_buffer.location.area,save_buffer.location.facing);
 save_buffer.gamma=gamma_level;save_buffer.animation=0;save_buffer.metadata_reserved=coop_campaign_epoch+1;
 if(floor)save_buffer.generated|=1u<<(floor-1);if(!save_metadata_valid((const SaveMeta*)&save_buffer)||!archive_write(0,floor,0))return 0;coop_campaign_epoch=save_buffer.metadata_reserved;return 1;
}
static __attribute__((minsize)) int UI_FN coop_campaign_personal_save(const CharacterState *hero){
 int reset=0;if(!save_read()){if(!coop_campaign_can_initialize()||!save_personal_begin(hero,0x83000001u))return 0;reset=1;}
 save_buffer.hero=*hero;if(save_buffer.hero.hp<=0)save_buffer.hero.hp=game_character_max_hp(&save_buffer.hero);capture_history();
 save_buffer.gamma=gamma_level;save_buffer.metadata_padding=options_volume_pack(audio_music_enabled?audio_music_volume:0,audio_effects_volume);save_buffer.metadata_padding3=options_dpad_pack(dpad_rotation);
 if(!save_metadata_valid((const SaveMeta*)&save_buffer))return 0;
 if(reset)campaign_initializing=1;int ok=archive_write(0,0,reset);if(ok)campaign_initializing=0;return ok;
}
