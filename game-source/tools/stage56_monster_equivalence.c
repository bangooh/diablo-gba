/* Host-only differential fixture. Compiled twice against unchanged source trees. */
#define MONSTER_TEST_ROOMS
#include "item-fixture.inc"
static uint8_t test_rooms[MAP_H][MAP_W];
int game_room_at(int x,int y){return (unsigned)x<MAP_W&&(unsigned)y<MAP_H?test_rooms[y][x]:-1;}
#include "../src/monster_ai.c"
#include <stdlib.h>
void game_generate_next(Game *g){(void)g;assert(!"unexpected region generation");}
void game_coop_actor_interaction(Game *g,unsigned pressed){(void)g;(void)pressed;}
static FILE *trace;
static unsigned snapshots;
void stage56_emit(const char *name,const void *bytes,size_t size){
 char tag[40]={0};assert(strlen(name)<sizeof tag);strcpy(tag,name);
 uint32_t length=(uint32_t)size;assert(length==size);
 assert(fwrite(tag,1,sizeof tag,trace)==sizeof tag);
 assert(fwrite(&length,1,sizeof length,trace)==sizeof length);
 assert(fwrite(bytes,1,size,trace)==size);
}
void stage56_spells(void),stage56_arrows(void),stage56_coop(void),stage56_exploration(void);
#define EMIT(value) stage56_emit(#value,&(value),sizeof(value))
static void checkpoint(unsigned scenario,unsigned tick,unsigned phase){
 unsigned context[3]={scenario,tick,phase};EMIT(context);
 EMIT(game);EMIT(hero_progress);EMIT(hero_inventory);EMIT(item_effects);
 EMIT(item_damage_min);EMIT(item_damage_max);EMIT(item_armor);EMIT(item_weapon_flags);
 EMIT(coop_players);EMIT(coop_active);EMIT(coop_id);EMIT(coop_members);EMIT(coop_turn);
 EMIT(coop_ticks);EMIT(coop_hash);EMIT(coop_campaign_active);EMIT(coop_campaign_epoch);EMIT(coop_region_request);
 EMIT(quest_progress);EMIT(dungeon_level);EMIT(campaign_seed);EMIT(butcher_quest);
 EMIT(monster_credit);EMIT(monster_history);EMIT(game_corpses);
 EMIT(walk);EMIT(wait);EMIT(delayed);EMIT(seen);EMIT(last_x);EMIT(last_y);EMIT(from_x);EMIT(from_y);
 EMIT(retreat);EMIT(rally);EMIT(special);EMIT(idle);EMIT(fraction);EMIT(away);EMIT(hurt);EMIT(path_count);EMIT(rng);
#if MONSTER_LEADER_PACKS
 EMIT(pack_link);
#endif
 /* Stack addresses are not state, but an escaped scratch pointer is a bug. */
 assert(!occupied_hint&&!walk_cache);
 stage56_spells();stage56_arrows();stage56_coop();stage56_exploration();
 EMIT(town_smith);EMIT(town_other);EMIT(town_premium);
 EMIT(hero_audio_event);EMIT(item_sound_events);EMIT(hazard_audio_event);EMIT(boss_audio_event);
 uint32_t hashes[4]={game_monsters_hash(2166136261u),game_spells_hash(2166136261u),
                     game_arrows_hash(2166136261u),game_shop_hash(2166136261u)};EMIT(hashes);
 /* Exercise actual archive packing without applying it to the live tick. */
 FloorState saved={0};int capture_ok=floor_capture(&saved,&game);assert(capture_ok);EMIT(saved);
 SaveMeta metadata={0};game_get_character(&game,&metadata.hero);
 metadata.location.x=game.x;metadata.location.y=game.y;metadata.location.area=game.area;
 metadata.location.facing=game.facing;metadata.location.reserved=dungeon_level;
 metadata.seed=campaign_seed;metadata.quest=butcher_quest;metadata.quests=quest_progress;
 memcpy(metadata.history,monster_history,sizeof monster_history);EMIT(metadata);
 snapshots++;
}
static void stale(unsigned i){
 walk[i]=3;wait[i]=4;delayed[i]=1;seen[i]=99;last_x[i]=39;last_y[i]=37;
 from_x[i]=game.enemies[i].x;from_y[i]=game.enemies[i].y;
 retreat[i]=1;rally[i]=2;special[i]=3;idle[i]=4;fraction[i]=5;away[i]=6;hurt[i]=2;path_count[i]=4;
 monster_credit[i]=(uint8_t)(1+(i%15));game.enemy_attack[i]=9;
}
static void prepare(unsigned scenario,unsigned members,unsigned population){
 fresh(ITEM_SHORT_SWORD);game_ai_reset();game_spell_new_campaign();
 memset(coop_players,0,sizeof coop_players);coop_region_clear();coop_ai_finish(&game);
 fresh(ITEM_SHORT_SWORD);game_ai_reset();clear_save_buffer();
 memset(&town_smith,0,sizeof town_smith);memset(town_other,0,sizeof town_other);memset(&town_premium,0,sizeof town_premium);
 memset(monster_history,0,sizeof monster_history);memset(test_rooms,0,sizeof test_rooms);
 memset(&quest_progress,0,sizeof quest_progress);campaign_seed=12345+scenario;dungeon_level=1;butcher_quest=0;
 game_set_difficulty(GAME_DIFFICULTY_NORMAL);game.x=40;game.y=40;
 game.target=game.swing_target=-1;game.hp=30000;game.mana=30000;
 hero_audio_event=hazard_audio_event=boss_audio_event=0;item_sound_events[0]=item_sound_events[1]=0;
 coop_ticks=coop_hash=0;coop_campaign_epoch=123;coop_campaign_active=1;
 /* Real worn weapon, varying classes/equipment and current stats per actor. */
 equip(ITEM_SHORT_SWORD,73);game.hp=30000;game.mana=30000;
 CharacterState hero={0};game_get_character(&game,&hero);
 for(unsigned p=0;p<4;p++){
  CharacterState h={0};game_create_hero(&h,p%3);h.hp=30000;h.mana=30000;
  h.progress.strength=70+p;h.progress.magic=70+p;h.progress.dexterity=70+p;
  coop_players[p]=(CoopPlayer){.hero=h,.x=40+(int)p*2,.y=40+(int)(p&1)*3,.facing=(uint8_t)p,.target=-1,.swing=-1};
 }
 coop_active=members>1;coop_members=(1u<<members)-1;coop_id=members>1?members-1:0;coop_turn=0;
 if(coop_active)coop_select(&game,coop_id);else game_apply_character(&game,&hero);
 /* All58 valid kinds occur in dead/dormant records, including slot111. */
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  unsigned kind=i%MONSTER_TYPES,alive=population==0?0:population==1?2:(i%3==0?2:0);
  unsigned hp=game_monster_hp_bound(kind,1);if(hp>511&&saved_rolled(kind))hp=511;
  game.enemies[i]=(Enemy){(uint8_t)(18+i%14),(uint8_t)(18+i/14),(int16_t)hp,(uint8_t)alive,(uint8_t)kind,(uint16_t)hp};
  stale(i);
 }
 if(population>=2){
  /* Aligned dead runs coexist with active/dormant groups in the same tick. */
  for(unsigned i=24;i<32;i++)game.enemies[i].alive=0;
  for(unsigned i=40;i<44;i++)game.enemies[i].alive=0;
  static const uint8_t kinds[]={0,2,15,19,23,28,33,40,45,50,54,47};
  static const uint8_t ids[]={0,4,11,23,37,50,68,79,91,100,108,111};
  for(unsigned j=0;j<sizeof ids;j++){
   unsigned i=ids[j],kind=kinds[j],hp=game_monster_hp_bound(kind,1);
   game.enemies[i]=(Enemy){(uint8_t)(34+j%4*3),(uint8_t)(34+j/4*3),(int16_t)hp,1,(uint8_t)kind,(uint16_t)hp};
   game_monster_reset(i);seen[i]=100;last_x[i]=game.x;last_y[i]=game.y;
   from_x[i]=game.enemies[i].x;from_y[i]=game.enemies[i].y;
   if(j%3==0){walk[i]=2;from_x[i]--;}
   if(j%3==1)wait[i]=2;
   if(j%3==2)hurt[i]=1;
   game.enemy_attack[i]=0;
  }
  assert(game_pack_assign(&game,108,100));
 }
 /* An explicitly dormant charge must stop, including stale origin state. */
 if(population){unsigned i=109;game.enemies[i].kind=23;game.enemies[i].alive=2;
  retreat[i]=RHINO_CHARGE;idle[i]=6;delayed[i]=0;fraction[i]=134;away[i]=7;rally[i]=160;
 }
 game.props[0]=(Prop){37,37,PROP_DOOR_L,0};game.props[1]=(Prop){39,37,PROP_SARC,1};
 game.props[2]=(Prop){35,42,PROP_BARREL,0};
 for(int y=28;y<49;y++)if(y!=38&&y!=44)game.map[y][46]=0;
 for(int y=0;y<MAP_H;y++)for(int x=43;x<MAP_W;x++)test_rooms[y][x]=1;
 for(unsigned i=0;i<8;i++)game_corpses[i]=(Corpse){(uint16_t)(i%2?1:CORPSE_TICKS+12),30+i,31,(uint8_t)(i%4),(uint8_t)i};
 rng=0x641af17u+scenario;
}
static void run_case(unsigned scenario,unsigned members,unsigned population,unsigned full){
 prepare(scenario,members,population);checkpoint(scenario,0,0);
 for(unsigned tick=1;tick<=64;tick++){
  if(full){
   if(coop_active){uint16_t keys[4]={0};for(unsigned p=0;p<members;p++)keys[p]=tick%24<8?KEY_A:tick%24<12?KEY_RIGHT:0;coop_sim_step(&game,keys);}
   else {unsigned held=tick%24<8?KEY_A:tick%24<12?KEY_RIGHT:0;unsigned previous=(tick-1)%24<8?KEY_A:(tick-1)%24<12?KEY_RIGHT:0;game_step(&game,held,held&~previous);}
  }else{
   game.ticks++;game_arrows_step(&game);game_monsters_step(&game);
   checkpoint(scenario,tick,1); /* Observe selected hero before the finish hook. */
   if(coop_active){coop_ai_finish(&game);coop_checksum(&game);}
  }
  game_explore_update(&game);checkpoint(scenario,tick,2);
 }
}
int main(int argc,char **argv){
 assert(argc==2);trace=fopen(argv[1],"wb");assert(trace);
 run_case(0,1,0,0);run_case(1,1,1,0);run_case(2,1,2,0);
 run_case(3,2,0,0);run_case(4,2,2,0);run_case(5,4,2,0);
 run_case(6,1,2,1);run_case(7,2,2,1);run_case(8,4,2,1);
 assert(!fclose(trace));printf("{\"cases\":9,\"ticks\":576,\"snapshots\":%u}\n",snapshots);
 return 0;
}
