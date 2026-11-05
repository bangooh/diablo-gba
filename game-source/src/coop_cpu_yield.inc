#if defined(__arm__)
#define CPU_YIELD_CODE __attribute__((target("thumb"),minsize,noinline))
#else
#define CPU_YIELD_CODE __attribute__((minsize,noinline))
#endif
/* Use acknowledged human inputs, never local hardware state. No persistent
 * path buffers or timers: inspect four humans and at most eight neighbours. */
static CPU_YIELD_CODE int cpu_human_path(int x,int y,const uint16_t keys[4]){
 for(unsigned i=0;i<4;i++)if((coop_members&~coop_cpu_mask&(1u<<i))&&coop_players[i].hero.hp>0){
  const CoopPlayer *p=&coop_players[i];
  if(p->x==x&&p->y==y)return 1;
  int sx=!!(keys[i]&KEY_RIGHT)-!!(keys[i]&KEY_LEFT),sy=!!(keys[i]&KEY_DOWN)-!!(keys[i]&KEY_UP);
  int dx=sx+sy,dy=sy-sx;dx=(dx>0)-(dx<0);dy=(dy>0)-(dy<0);
  if((dx||dy)&&p->x+dx==x&&p->y+dy==y)return 1;
 }
 return 0;
}
static CPU_YIELD_CODE int coop_cpu_yield(Game *g,unsigned id,const uint16_t keys[4]){
 if(g->hp<=0||g->move_wait||g->attack_wait||g->hit_flash)return -1;
 if(!cpu_human_path(g->x,g->y,keys))return -1;
 static const int8_t dx[8]={1,0,-1,-1,-1,0,1,1},dy[8]={1,1,1,0,-1,-1,-1,0};
 static const uint16_t move[8]={KEY_DOWN,KEY_DOWN|KEY_LEFT,KEY_LEFT,KEY_LEFT|KEY_UP,KEY_UP,KEY_UP|KEY_RIGHT,KEY_RIGHT,KEY_RIGHT|KEY_DOWN};
 for(unsigned d=0;d<8;d++){
  int x=g->x+dx[d],y=g->y+dy[d];
  if(cpu_human_path(x,y,keys)||!game_walkable(g,x,y)||coop_blocks(x,y,id)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0)||!cpu_line(g,g->x,g->y,x,y))continue;
  /* Match the controller's strict actor/monster corner checks as well. */
  if(dx[d]&&dy[d]&&(coop_blocks(x,g->y,id)||coop_blocks(g->x,y,id)||game_monster_blocks(g,x,g->y,-1)||game_monster_blocks(g,g->x,y,-1)))continue;
  return move[d];
 }
 return 0; /* Human movement still passes through a trapped CPU. */
}

#undef CPU_YIELD_CODE
