/* CPU actors generate ordinary player input; they never apply attack damage.
 * Initial CPU party is offline: host human + warrior/rogue/sorcerer. */
static int cpu_abs(int n){return n<0?-n:n;}
/* Share one deterministic leader rule for pursuit, recall and resurrection.
 * Human slots only; equal distances keep the lower slot. No per-CPU cache. */
static __attribute__((minsize,noinline)) const CoopPlayer *cpu_leader(const Game *g){
 const CoopPlayer *leader=0;int best=10000;
 for(unsigned i=0;i<4;i++)if((coop_members&~coop_cpu_mask&(1u<<i))&&coop_players[i].hero.hp>0){
  const CoopPlayer *p=&coop_players[i];int dx=cpu_abs(g->x-p->x),dy=cpu_abs(g->y-p->y),d=dx>dy?dx:dy;
  if(d<best){best=d;leader=p;}
 }
 return leader;
}
static __attribute__((minsize,noinline)) int cpu_line(Game *g,int x,int y,int tx,int ty){
 int dx=cpu_abs(tx-x),dy=cpu_abs(ty-y),sx=(tx>x)-(tx<x),sy=(ty>y)-(ty<y),err=dx-dy;
 for(unsigned n=0;n<80;n++){
  if(x==tx&&y==ty)return 1;int ox=x,oy=y,v=err*2;
  if(v>-dy){err-=dy;x+=sx;}if(v<dx){err+=dx;y+=sy;}
  if(!game_walkable(g,x,y)||(x!=ox&&y!=oy&&(!game_walkable(g,x,oy)||!game_walkable(g,ox,y))))return 0;
 }return 0;
}
unsigned __attribute__((minsize,noinline)) coop_cpu_input(Game *g,unsigned id){
 if(g->hp<=0||g->move_wait||g->attack_wait||g->hit_flash)return 0;
 if(g->hp<game_max_hp()/3&&g->potions)return (coop_players[id].previous&KEY_L)?0:KEY_L;
 const CoopPlayer *leader=cpu_leader(g);if(!leader)return 0;
 int tx=leader->x,ty=leader->y,best=-1,dist=10000;
 unsigned cls=game_hero_class();
 if(!g->area)for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){
  unsigned i=coop_enemy_index(g,n);
  const Enemy *e=&g->enemies[i];if(e->alive!=1||game_enemy_hidden(g,i))continue;
  /* Use the same acquisition diamond as spell control. Otherwise a sleeping
   * gargoyle outside the screen keeps the sorcerer casting at empty space. */
  if(cls==2&&!game_target_visible(g,i))continue;
  int dx=e->x-g->x,dy=e->y-g->y,d=dx*dx+dy*dy;
  /* Sorcerer visibility already proves the same strict Bresenham path. */
  if(cpu_abs(dx)>8||cpu_abs(dy)>8||d>=dist||(cls!=2&&!cpu_line(g,g->x,g->y,e->x,e->y)))continue;
  best=i;dist=d;
 }
 if(best>=0){
  const Enemy *e=&g->enemies[best];int dx=e->x-g->x,dy=e->y-g->y;
  if(cls!=0){
   if(cls==2){
    if(cpu_abs(dx)>2||cpu_abs(dy)>2){
     if(game_staff_slot()>=0){g->target=best;return (coop_players[id].previous&KEY_B)?0:KEY_B;}
     return 0;
    }
   }else if(cpu_abs(dx)>2||cpu_abs(dy)>2)return KEY_A;
   tx=g->x-dx;ty=g->y-dy;
  }else{if(cpu_abs(dx)<=1&&cpu_abs(dy)<=1)return KEY_A;tx=e->x;ty=e->y;}
 }else if(cpu_abs(tx-g->x)<=2&&cpu_abs(ty-g->y)<=2)return 0;
 static const int8_t dx8[8]={1,0,-1,-1,-1,0,1,1},dy8[8]={1,1,1,0,-1,-1,-1,0};
 static const uint16_t keys[8]={KEY_DOWN,KEY_DOWN|KEY_LEFT,KEY_LEFT,KEY_LEFT|KEY_UP,KEY_UP,KEY_UP|KEY_RIGHT,KEY_RIGHT,KEY_RIGHT|KEY_DOWN};
 int dx=(tx>g->x)-(tx<g->x),dy=(ty>g->y)-(ty<g->y);
 unsigned direction=dy>0?(dx>0?0:dx<0?2:1):dy<0?(dx>0?6:dx<0?4:5):dx<0?3:7;
 int choice=-1,score=100000;
 for(unsigned n=0;n<3;n++){
  unsigned d=n==0?direction:n==1?(direction+7)&7:(direction+1)&7;
  int x=g->x+dx8[d],y=g->y+dy8[d];
  if(!game_walkable(g,x,y)||coop_blocks(x,y,id)||game_monster_blocks(g,x,y,-1)||game_spell_golem_at(x,y,0))continue;
  /* The destination is one cell away and was just checked. Only the two
   * diagonal corner cells remain; do not trace/check the destination twice. */
  if(x!=g->x&&y!=g->y&&(!game_walkable(g,x,g->y)||!game_walkable(g,g->x,y)))continue;
  int a=tx-x,b=ty-y,v=a*a+b*b;if(!n){choice=d;break;}if(v<score){score=v;choice=d;}
 }
 if(choice<0)return best>=0&&cls==1?KEY_A:0;
 return keys[choice];
}
