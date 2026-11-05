/* MAI_Path / M_PathWalk / MonstCheckDoors, retail monster.cpp and objects.cpp.
 * Included after call_walk. path_count lives beside the actor state clocks and
 * participates in reset/hash. Unlike native FindPath, this bounded search examines
 * a41x41 window using weighted A*, at most384 expansions and128 frontier
 * entries. This GBA adaptation is not the original FindPath implementation.
 * Its bounded scratch is on the stack; search never mutates the world or RNG.
 * Native flags are mapped through monsters.json.source_id, including uniques.
 * All six native L1/L2/L3 door orientations use the same approach contract. */
enum { MONSTER_PATH_SIDE=41,MONSTER_PATH_CELLS=1681,MONSTER_PATH_NODES=128,
       MONSTER_PATH_BUDGET=384,MONSTER_PATH_FREE=0,MONSTER_PATH_BLOCKED=1,MONSTER_PATH_VISITED=2,
       MONSTER_PATH_SEARCH=1,MONSTER_PATH_DOORS=2 };
#define monster_path_flags monster71_path
static AI_ROM void monster_check_doors(Game *g,int i){
 const Enemy *e=&g->enemies[i];
 if(!(monster_path_flags[game_monster_base_kind(e->kind)]&MONSTER_PATH_DOORS))return;
 for(int p=0;p<MAX_PROPS;p++){
  const Prop *door=&g->props[p];if(door->state)continue;
  int dx=abs_i(door->x-e->x),dy=abs_i(door->y-e->y);
  if(game_prop_is_door(door->kind)&&(game_prop_door_axis(door->kind)?dx<=1&&dy==1:dx==1&&dy<=1)){
   game_open_prop(g,p);if(door->state)walk_cache_clear();
  }
 }
}
/* PosOkMonst2 tests terrain/objects, not actor bodies. The endpoint is the
 * selected player's current cell. Keep the port's strict diagonal corners. */
static AI_ROM int monster_path_clear(const Game *g,int i,int tx,int ty){
 const Enemy *e=&g->enemies[i];int x=e->x,y=e->y,dx=abs_i(tx-x),dy=abs_i(ty-y);
 int sx=x<tx?1:-1,sy=y<ty?1:-1,err=dx-dy;
 while(x!=tx||y!=ty){
  int nx=x,ny=y,v=2*err;if(v>-dy){err-=dy;nx+=sx;}if(v<dx){err+=dx;ny+=sy;}
  if(!monster_walkable(g,nx,ny)||((nx!=tx||ny!=ty)&&!monster_hazard_free(g,i,nx,ny))||(nx!=x&&ny!=y&&(!monster_walkable(g,nx,y)||!monster_hazard_free(g,i,nx,y)||!monster_walkable(g,x,ny)||!monster_hazard_free(g,i,x,ny))))return 0;
  x=nx;y=ny;
 }
 return 1;
}
static unsigned monster_path_state(const uint8_t *cells,unsigned p){return(cells[p>>2]>>((p&3)*2))&3;}
static void monster_path_set(uint8_t *cells,unsigned p,unsigned state){unsigned shift=(p&3)*2;cells[p>>2]=(cells[p>>2]&~(3u<<shift))|(state<<shift);}
static void monster_path_block(uint8_t *cells,int ox,int oy,int x,int y){
 unsigned nx=(unsigned)(x-ox),ny=(unsigned)(y-oy);
 if(nx<MONSTER_PATH_SIDE&&ny<MONSTER_PATH_SIDE)monster_path_set(cells,ny*MONSTER_PATH_SIDE+nx,MONSTER_PATH_BLOCKED);
}
static int monster_path_cell(uint8_t *cells,const Game *g,int i,int ox,int oy,int x,int y){
 if((unsigned)x>=MONSTER_PATH_SIDE||(unsigned)y>=MONSTER_PATH_SIDE)return 0;
 unsigned p=y*MONSTER_PATH_SIDE+x;if(monster_path_state(cells,p)==MONSTER_PATH_BLOCKED)return 0;
 if(!monster_hazard_free(g,i,ox+x,oy+y)){monster_path_set(cells,p,MONSTER_PATH_BLOCKED);return 0;}
 return 1;
}
static unsigned monster_path_distance(int x,int y,int tx,int ty){unsigned dx=abs_i(tx-x),dy=abs_i(ty-y);return dx>dy?dx:dy;}
/* Packed frontier: local cell11bits, first direction3bits, distance8bits,
 * priority9bits. A numeric min-heap gives stable ties without RNG or pointers. */
static void monster_path_push(uint32_t *heap,unsigned *count,uint32_t node){
 unsigned p=(*count)++;while(p){unsigned up=(p-1)/2;if(heap[up]<=node)break;heap[p]=heap[up];p=up;}heap[p]=node;
}
static uint32_t monster_path_pop(uint32_t *heap,unsigned *count){
 uint32_t node=heap[0],last=heap[--*count];unsigned p=0;
 while(2*p+1<*count){unsigned child=2*p+1;if(child+1<*count&&heap[child+1]<heap[child])child++;if(last<=heap[child])break;heap[p]=heap[child];p=child;}heap[p]=last;return node;
}
#ifdef MONSTER_PATH_PROFILE
static unsigned monster_path_expanded,monster_path_frontier_peak;
#endif
static AI_ROM int monster_path_step(const Game *g,int i,int tx,int ty){
 /*421-byte two-bit map +512-byte frontier; no static world/actor allocation.
  * Block terrain in one pass, then props and actor reservations once each.
  * Firewall cells are queried lazily; a visited cell remains usable for
  * diagonal clearance, unlike a solid cell. */
 uint8_t cells[(MONSTER_PATH_CELLS+3)/4]={0};uint32_t heap[MONSTER_PATH_NODES];
 const Enemy *e=&g->enemies[i];int ox=e->x-MONSTER_PATH_SIDE/2,oy=e->y-MONSTER_PATH_SIDE/2;
 if((unsigned)tx>=MAP_W||(unsigned)ty>=MAP_H)return -1;
 for(int y=0;y<MONSTER_PATH_SIDE;y++)for(int x=0;x<MONSTER_PATH_SIDE;x++){
  unsigned wx=(unsigned)(ox+x),wy=(unsigned)(oy+y);
  if(wx>=MAP_W||wy>=MAP_H||!g->map[wy][wx])monster_path_set(cells,y*MONSTER_PATH_SIDE+x,MONSTER_PATH_BLOCKED);
 }
 for(int p=0;p<MAX_PROPS;p++){
  const Prop *prop=&g->props[p];
  /* Native PosOkMonst3 allows future door cells; the first move still uses
   * can_step after MonstCheckDoors has opened an approachable door. */
  if((monster_path_flags[game_monster_base_kind(e->kind)]&MONSTER_PATH_DOORS)&&game_prop_is_door(prop->kind))continue;
  if(game_prop_is_large_fountain(prop->kind)){
   for(int y=(int)prop->y-1;y<=(int)prop->y;y++)for(int x=(int)prop->x-1;x<=(int)prop->x;x++)monster_path_block(cells,ox,oy,x,y);
   continue;
  }
  if(game_prop_blocks(prop,prop->x,prop->y))monster_path_block(cells,ox,oy,prop->x,prop->y);
  if(prop->kind==PROP_SARC)monster_path_block(cells,ox,oy,prop->x,(int)prop->y-1);
 }
 for(int n=0;n<MAX_ENEMIES;n++)if(n!=i&&g->enemies[n].alive==1){
  monster_path_block(cells,ox,oy,g->enemies[n].x,g->enemies[n].y);
  if(walk[n])monster_path_block(cells,ox,oy,from_x[n],from_y[n]);
 }
 monster_path_block(cells,ox,oy,g->x,g->y);
 if(coop_active)for(unsigned n=0;n<4;n++)if((coop_members&(1u<<n))&&coop_players[n].hero.hp>0)
  monster_path_block(cells,ox,oy,coop_players[n].x,coop_players[n].y);
 for(unsigned owner=0;owner<4;owner++){
  int gx,gy;if(game_spell_golem_position(owner,&gx,&gy))monster_path_block(cells,ox,oy,gx,gy);
 }
 unsigned center=MONSTER_PATH_CELLS/2,count=0,expanded=0,start=monster_path_distance(e->x,e->y,tx,ty),best=start;
 int partial=-1,far_target=(unsigned)(tx-ox)>=MONSTER_PATH_SIDE||(unsigned)(ty-oy)>=MONSTER_PATH_SIDE;
 monster_path_set(cells,center,MONSTER_PATH_VISITED);monster_path_push(heap,&count,center|(2*start<<22));
#ifdef MONSTER_PATH_PROFILE
 monster_path_expanded=0;monster_path_frontier_peak=1;
#endif
 while(count&&expanded<MONSTER_PATH_BUDGET){
  uint32_t node=monster_path_pop(heap,&count);unsigned p=node&2047,first=(node>>11)&7,depth=(node>>14)&255;expanded++;
#ifdef MONSTER_PATH_PROFILE
  monster_path_expanded=expanded;
#endif
  int x=p%MONSTER_PATH_SIDE,y=p/MONSTER_PATH_SIDE,dx=tx-ox-x,dy=ty-oy-y,heading=facing(dx,dy);
  if(abs_i(dx)<=1&&abs_i(dy)<=1&&(!dx||!dy||
     (monster_path_cell(cells,g,i,ox,oy,x+dx,y)&&monster_path_cell(cells,g,i,ox,oy,x,y+dy))))return depth?(int)first:-1;
  unsigned distance=monster_path_distance(ox+x,oy+y,tx,ty);
  /* A partial route must start by strictly reducing target distance. This
   * prevents bounded searches from alternating sideways or walking away.
   * A complete route may legitimately begin away from a target to detour. */
  if(depth&&distance<best&&monster_path_distance(e->x+vx[first],e->y+vy[first],tx,ty)<start){best=distance;partial=first;}
  if(depth==255)continue;
  static const int8_t turns[8]={0,1,-1,2,-2,3,-3,4};
  for(int n=0;n<8;n++){
   int d=(heading+turns[n]+8)&7,nx=x+vx[d],ny=y+vy[d];
   if(!monster_path_cell(cells,g,i,ox,oy,nx,ny))continue;
   unsigned next=ny*MONSTER_PATH_SIDE+nx;if(monster_path_state(cells,next)!=MONSTER_PATH_FREE)continue;
   if(vx[d]&&vy[d]&&(!monster_path_cell(cells,g,i,ox,oy,nx,y)||!monster_path_cell(cells,g,i,ox,oy,x,ny)))continue;
   if(!depth&&(!can_step(g,i,d)||game_spell_golem_at(ox+nx,oy+ny,0)))continue;
   if(count==MONSTER_PATH_NODES)continue;
   unsigned f=depth?first:(unsigned)d,cost=depth+1,priority=cost+2*monster_path_distance(ox+nx,oy+ny,tx,ty);
   monster_path_set(cells,next,MONSTER_PATH_VISITED);monster_path_push(heap,&count,next|(f<<11)|(cost<<14)|(priority<<22));
#ifdef MONSTER_PATH_PROFILE
   if(count>monster_path_frontier_peak)monster_path_frontier_peak=count;
#endif
  }
 }
 return far_target?partial:-1;
}
static AI_ROM int monster_path_think(Game *g,int i){
 const Enemy *e=&g->enemies[i];
 if(e->alive!=1||!seen[i]||walk[i]||wait[i]||hurt[i]||g->enemy_attack[i]||
    special[i]||retreat[i]||game_enemy_hidden(g,i)||game_spell_stoned(i)||game_finale_protected(e->kind)){
  path_count[i]=0;return 0;
 }
 /* Retail MAI_Path/M_PathWalk use _menemyx/y, refreshed from the selected
  * player's future position every tick. _lastx/y only steer ordinary AI.
  * A SEARCH monster must not stop at a stale point after a corner. */
 int tx=g->x,ty=g->y;
 int clear=monster_path_clear(g,i,tx,ty);
 if(!clear||(path_count[i]>=5&&path_count[i]<8))monster_check_doors(g,i);
 if(!(monster_path_flags[game_monster_base_kind(e->kind)]&MONSTER_PATH_SEARCH)){path_count[i]=0;return 0;}
 if(!clear||(path_count[i]>=5&&path_count[i]<8)){
  if(path_count[i]<255)path_count[i]++;
  if(path_count[i]<5)return 0;
  int d=monster_path_step(g,i,tx,ty);
  if(d>=0){start_walk(g,i,d);return 1;}
 }
 path_count[i]=0;return 0;
}
