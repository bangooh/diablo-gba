/* Command barrier selects the requesting actor before calling this helper.
 * Locate the existing upstairs entrance, never regenerate/reload the floor. */
#if defined(__arm__)
__attribute__((target("thumb")))
#endif
int __attribute__((minsize,noinline)) coop_restart_entrance(Game *g){
 if(!coop_active||!coop_campaign_active||coop_turn>=4||(coop_cpu_mask&(1u<<coop_turn))||g->hp>0||g->won||coop_region_pending())return 0;
 int ox=g->x,oy=g->y;
 if(g->area){g->x=25;g->y=31;}else game_dungeon_spawn(g);
 int sx=g->x,sy=g->y;g->x=ox;g->y=oy;
 for(int r=0;r<=8;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  if(cpu_abs(dx)!=r&&cpu_abs(dy)!=r)continue;
  int x=sx+dx,y=sy+dy;
  if(!game_walkable(g,x,y)||coop_blocks(x,y,-1)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0)||!cpu_line(g,sx,sy,x,y))continue;
  game_spell_revive(g);g->x=x;g->y=y;
  g->attack_wait=g->move_wait=g->hit_flash=g->flash=0;
  g->attack_queued=g->attack_duration=g->reaction_duration=g->reaction_block=0;
  g->target=g->swing_target=-1;g->pickup_notice_ticks=g->town_message=0;
  coop_players[coop_turn].previous=0;cpu_refill(g);return 1;
 }
 return 0;
}
