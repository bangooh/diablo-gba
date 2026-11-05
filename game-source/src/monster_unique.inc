/* Stable identities for all72 enabled retail ordinary uniques and their
 * explicit minion aliases. Restore never guesses membership from position. */
#define UNIQUE_ROM __attribute__((section(".text.ui"),noinline))
UNIQUE_ROM int game_monster_is_unique(unsigned k){return(k>=50&&k<54)||(k>=105&&k<173);}
UNIQUE_ROM int game_monster_is_pack_minion(unsigned k){return(k>=54&&k<58)||(k>=173&&k<241);}
UNIQUE_ROM unsigned game_monster_base_kind(unsigned k){return k<MONSTER_TYPES?monster71_base[k]:0;}
UNIQUE_ROM unsigned game_monster_corpse_kind(unsigned k){return game_monster_is_pack_minion(k)?game_monster_base_kind(k):k;}
UNIQUE_ROM unsigned game_unique_minion_kind(unsigned k){return k>=50&&k<54?k+4:k>=105&&k<173?k+68:0;}
UNIQUE_ROM unsigned game_unique_leader_kind(unsigned k){return k>=54&&k<58?k-4:k>=173&&k<241?k-68:0;}
UNIQUE_ROM unsigned game_unique_palette_family(unsigned k){return monster_defs[game_monster_base_kind(k)].family==3?4:3;}
UNIQUE_ROM int game_unique_base_on_floor(unsigned base,unsigned floor){
 for(unsigned i=0;i<72;i++){unsigned k=i<4?50+i:101+i;if(game_monster_base_kind(k)==base&&monster_defs[k].min_floor==floor)return 1;}return 0;
}
UNIQUE_ROM unsigned game_unique_for_roster(unsigned floor,const uint8_t *roster,unsigned count){
 /* One translated leader per scene, selected fairly among eligible native
  * rows. Six actor bank pairs remain available to the regular population. */
 unsigned start=item_seed(campaign_seed^floor*0x9e3779b9u)%72;
 for(unsigned n=0;n<72;n++){
  unsigned at=(n+start)%72,k=at<4?50+at:101+at;
  if(monster_defs[k].min_floor!=floor)continue;
  for(unsigned j=0;j<count;j++)if(roster[j]==game_monster_base_kind(k))return k;
 }
 return 0;
}
UNIQUE_ROM void game_unique_restore(const Game *g){
 game_pack_clear();if(g->area)return;
 for(unsigned group=0;group<72;group++){
  unsigned k=group<4?50+group:101+group,member=game_unique_minion_kind(k),leader=MAX_ENEMIES,leaders=0,members=0;
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive){if(g->enemies[i].kind==k){leader=i;leaders++;}else if(g->enemies[i].kind==member)members++;}
  if(leaders!=1||members>8||!monster71_unique_leashed[k])continue;
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive&&g->enemies[i].kind==member)game_pack_assign(g,i,leader);
 }
}
#undef UNIQUE_ROM
