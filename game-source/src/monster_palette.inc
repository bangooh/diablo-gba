/* Palette ownership follows the actual scene, including dormant quest actors.
 * Retain dead records too: container spawns reuse the stored family variant.
 * Old floor13 saves may already mix Black Knights with another family5 body.
 * Their unused highlight bank can hold the guard without changing the save. */
static uint8_t monster_guard_shared;
static UI_FN unsigned monster_palette_bank(unsigned kind,unsigned selected){
 const MonsterArt *a=&monster_art[kind];
 if(kind==MONSTER_BLACK_KNIGHT){
  if(dungeon_level==16)return selected?4:1;
  if(monster_guard_shared)return 14;
 }
 if(monster_guard_shared&&a->bank==10)return 10;
 return selected?a->bright:a->bank;
}
static UI_FN __attribute__((target("thumb"),minsize)) void monster_palette_load(void){
 monster_guard_shared=0;
 if(dungeon_level==13){
  unsigned guard=0,other=0;
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].alive||game.enemies[i].x||game.enemies[i].y){
   unsigned k=game.enemies[i].kind;
   guard|=k==MONSTER_BLACK_KNIGHT;
   other|=k!=MONSTER_BLACK_KNIGHT&&monster_art[k].bank==10;
  }
  monster_guard_shared=guard&&other;
 }
 /* Empty families still support later container spawns. Actual actors win
  * in the second pass, so quest-only rows never fall back to normal types. */
 for(unsigned family=0;family<7;family++){
  unsigned k=game_monster_variant(&game,family);
  if((!k&&family)||(k==6&&!(dungeon_level==2&&butcher_quest))||(k==5&&dungeon_level==2&&butcher_quest))continue;
  const MonsterArt *a=&monster_art[k];lz_decode(a->palette,load_buffer,sizeof(load_buffer));
  dma(load_buffer,(void*)(0x05000200+a->bank*32),32);
  dma((uint8_t*)load_buffer+32,(void*)(0x05000200+a->bright*32),32);
 }
 unsigned loaded=0;
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].alive||game.enemies[i].x||game.enemies[i].y){
  unsigned k=game.enemies[i].kind,b=monster_palette_bank(k,0),h=monster_palette_bank(k,1);
  unsigned bits=(1u<<b)|(1u<<h);if((loaded&bits)==bits)continue;
  lz_decode(monster_art[k].palette,load_buffer,sizeof(load_buffer));
  if(!(loaded&(1u<<b)))dma(load_buffer,(void*)(0x05000200+b*32),32);
  if(h!=b&&!(loaded&(1u<<h)))dma((uint8_t*)load_buffer+32,(void*)(0x05000200+h*32),32);
  loaded|=bits;
 }
}
