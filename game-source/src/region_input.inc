/* A fresh A press must operate the reachable door shown by the target/HUD
 * before considering nearby region triggers. Revalidate stale targets after
 * movement; open doors also own A even when an occupant prevents closing.
 * Keep travel cold and in ROM; the ordinary controller still consumes A. */
static __attribute__((section(".text.ui"),target("thumb"),minsize)) int game_try_travel(Game *g){
 int prop=game_prop_target(g);
 if(prop>=0&&game_prop_is_door(g->props[prop].kind)&&game_prop_direction(g,prop)>=0)return 0;
 if(!g->hit_flash&&game_spell_portal_use(g))return 1;
 if(!g->hit_flash&&!g->move_wait&&!g->attack_wait&&game_item_target(g)<0){
  int level=game_shortcut_target(g);if(level){game_region_shortcut(g,(unsigned)level);return 1;}
 }
 if(g->area){
  if(!g->move_wait&&abs_i(g->x-25)<=2&&abs_i(g->y-29)<=2){game_enter_dungeon(g);return 1;}
  return 0;
 }
 if(!g->hit_flash&&!g->move_wait&&!g->attack_wait&&game_item_target(g)<0){
  if(game_quest_entry(g))return 1;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
   int x=g->x+dx,y=g->y+dy;
   if(x>=0&&y>=0&&x<MAP_W&&y<MAP_H&&g->map[y][x]==2){
    if(game_campaign_layout()){
     if(dungeon_level>1)game_region_floor(g,dungeon_level==REGION_BONE?6:dungeon_level==REGION_KING?3:dungeon_level==REGION_WATER?2:dungeon_level-1);
     else game_enter_town(g,0);
    }
    return 1;
   }
  }
  if(g->down_x&&abs_i(g->x-g->down_x)<=2&&abs_i(g->y-g->down_y)<=2){
   if(game_campaign_layout()&&dungeon_level<16)game_region_floor(g,dungeon_level+1);
   else{g->town_message=250;g->pickup_notice_ticks=60;}
   return 1;
  }
 }
 return 0;
}
