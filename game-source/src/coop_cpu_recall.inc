#if defined(__arm__)
#define CPU_RECALL_CODE __attribute__((target("thumb"),minsize,noinline))
#else
#define CPU_RECALL_CODE __attribute__((minsize,noinline))
#endif
/* 20 Hz deterministic clocks; never use presentation FPS or heal on recall. */
static CPU_RECALL_CODE void coop_cpu_recall(Game *g,unsigned id,int moved){
 const CoopPlayer *p=g->hp>0?cpu_leader(g):0;
 if(!p||(cpu_abs(g->x-p->x)<8&&cpu_abs(g->y-p->y)<8)||g->attack_wait||g->hit_flash){cpu_far_ticks[id]=cpu_stuck_ticks[id]=0;return;}
 if(!g->area)for(unsigned i=0;i<MAX_ENEMIES;i++){
  const Enemy *e=&g->enemies[i];
  if(e->alive==1&&cpu_abs(e->x-g->x)<=8&&cpu_abs(e->y-g->y)<=8&&!game_enemy_hidden(g,i)&&cpu_line(g,g->x,g->y,e->x,e->y)){cpu_far_ticks[id]=cpu_stuck_ticks[id]=0;return;}
 }
 if(cpu_far_ticks[id]<600)cpu_far_ticks[id]++;
 if(moved)cpu_stuck_ticks[id]=0;
 else if(!g->move_wait&&cpu_stuck_ticks[id]<200)cpu_stuck_ticks[id]++;
 if(cpu_far_ticks[id]<600&&cpu_stuck_ticks[id]<200)return;
 for(int r=1;r<=2;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){
  if(cpu_abs(dx)!=r&&cpu_abs(dy)!=r)continue;int x=p->x+dx,y=p->y+dy;
  if(!game_walkable(g,x,y)||coop_blocks(x,y,-1)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0)||!cpu_line(g,p->x,p->y,x,y))continue;
  game_spell_cancel(g);g->x=x;g->y=y;g->attack_wait=g->move_wait=g->hit_flash=0;
  g->attack_queued=g->attack_duration=g->reaction_duration=g->reaction_block=0;
  g->target=g->swing_target=-1;coop_players[id].previous=0;
  cpu_far_ticks[id]=cpu_stuck_ticks[id]=0;return;
 }
}
#undef CPU_RECALL_CODE
