/* GBA adaptation of action gating / target retention ideas studied in
 * DevilutionX controls/plrctrls.cpp and player.cpp; independent implementation.
 * Bounded GBA path search; no SDL or network command dependencies. */
#include "game.h"
#include "coop.h"
#include "prop_collision.h"
#include "performance_options.h"
#if PERF_BATCH_WALK
#include "walk_batch.h"
#endif
/* Presentation hint only; excluded from saves and co-op checksums. */
uint8_t game_attack_repeat[4];
static int abs_i(int n){return n<0?-n:n;}
static int sign(int n){return (n>0)-(n<0);}
static int direction(int dx,int dy){
    int sx=dx-dy,sy=dx+dy;
    if(!sx)return sy>=0?0:4;
    if(!sy)return sx>0?6:2;
    return sy>0?(sx>0?7:1):(sx>0?5:3);
}
static int reachable(const Game *g,int i){
    if(i<0 || i>=MAX_ENEMIES || g->enemies[i].alive!=1||game_enemy_hidden(g,i))return 0;
    int dx=g->enemies[i].x-g->x,dy=g->enemies[i].y-g->y;
    if(abs_i(dx)>1 || abs_i(dy)>1)return 0;
    /* Do not hit through blocked corners. */
    return !dx || !dy || (game_walkable(g,g->x+dx,g->y) && game_walkable(g,g->x,g->y+dy));
}
int game_prop_direction(const Game *g,int i){
 if(i>=0&&i<MAX_PROPS&&g->props[i].kind>=PROP_DECOR_FIRST&&g->props[i].kind<=PROP_DECOR_LAST)return -1;
 if(g->area||i<0||i>=MAX_PROPS||!g->props[i].kind||(g->props[i].state&&!game_prop_repeatable(g->props[i].kind))||game_prop_is_wall_trap(g->props[i].kind)||game_prop_is_theme_decor(g->props[i].kind)||g->props[i].kind==PROP_BRAZIER||g->props[i].kind==PROP_WATER_CANDLE||g->props[i].kind==PROP_VILE_CIRCLE||g->props[i].kind==PROP_VILE_ALTAR)return -1;
 const Prop *p=&g->props[i];int best=-1,score=10;
 int wide=game_prop_is_large_fountain(p->kind),width=wide?2:1,height=(wide||p->kind==PROP_SARC)?2:1;
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  int dx=(int)p->x-x-g->x,dy=(int)p->y-y-g->y;
  if(abs_i(dx)>1||abs_i(dy)>1||(!dx&&!dy))continue;
  if(game_prop_is_door(p->kind)&&(game_prop_door_axis(p->kind)?abs_i(dy)!=1:abs_i(dx)!=1))continue;
  /* Retail OperateL1/L2/L3Door permits the adjacent diagonal on its axis.
   * Walking/attacks retain their strict corner test; a door handle does not. */
  if(!game_prop_is_door(p->kind)&&dx&&dy&&(!game_walkable(g,g->x+dx,g->y)||!game_walkable(g,g->x,g->y+dy)))continue;
  int d=direction(dx,dy),r=abs_i(d-g->facing);if(r>4)r=8-r;
  if(r<score){best=d;score=r;}
 }
 return best;
}
#if !PERF_BATCH_WALK
static int target_cell_clear(const Game *g,int x,int y){
 if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H||!g->map[y][x])return 0;
 for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&g->props[i];
  if(coop_active&&game_prop_is_target_container(p->kind))continue;
  if(game_prop_blocks(p,x,y))return 0;
 }
 return 1;
}
#endif
/* Shared ranged visibility contract: alive, on screen, unobstructed shot.
 * Strict corner traversal also prevents shooting through diagonal walls. */
int game_target_visible(const Game *g,int i){
 if(g->area||i<0||i>=MAX_ENEMIES||g->enemies[i].alive!=1||g->enemies[i].hp<=0||game_enemy_hidden(g,i))return 0;
 int x=g->x,y=g->y,tx=g->enemies[i].x,ty=g->enemies[i].y;
 int dx=tx-x,dy=ty-y;
 if(abs_i(dx)>8||abs_i(dy)>8||abs_i(dx-dy)>7||dx+dy < -8||dx+dy > 6)return 0;
 int sx=sign(dx),sy=sign(dy);dx=abs_i(dx);dy=abs_i(dy);int err=dx-dy;
#if PERF_BATCH_WALK
 uint16_t cells[24];unsigned n=0;
#endif
 while(x!=tx||y!=ty){int nx=x,ny=y,e=err*2;if(e>-dy){err-=dy;nx+=sx;}if(e<dx){err+=dx;ny+=sy;}
#if PERF_BATCH_WALK
  if((unsigned)nx>=MAP_W||(unsigned)ny>=MAP_H)return 0;
  cells[n++]=(uint16_t)(nx|(ny<<8));
  if(nx!=x&&ny!=y){
   if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H)return 0;
   cells[n++]=(uint16_t)(nx|(y<<8));cells[n++]=(uint16_t)(x|(ny<<8));
  }
#else
  if(!target_cell_clear(g,nx,ny)||(nx!=x&&ny!=y&&(!target_cell_clear(g,nx,y)||!target_cell_clear(g,x,ny))))return 0;
#endif
  x=nx;y=ny;
 }
#if PERF_BATCH_WALK
 /* Players never enter sight tests. Multiplayer containers are transparent. */
 return walk_batch_clear_filtered(g,cells,n,coop_active);
#else
 return 1;
#endif
}
int game_ranged_target(const Game *g,int manual){
 if(g->area)return -1;
 int best=-1,score=10000;
 /* Empty/dead slots dominate late-floor scans. Reject before the ROM call. */
 for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){int i=(int)coop_enemy_index(g,n);if(g->enemies[i].alive==1&&g->enemies[i].hp>0&&game_target_visible(g,i)){
  int dx=g->enemies[i].x-g->x,dy=g->enemies[i].y-g->y;
  int r=abs_i(direction(dx,dy)-g->facing);if(r>4)r=8-r;
  if(manual&&r)continue;
  int n=(dx*dx+dy*dy)*8+r;if(n<score){score=n;best=i;}
 }
 }
 return best;
}
int game_item_target(const Game *g){
 if(g->hp<=0)return -1;
 int best=-1,score=100;
 for(int i=0;i<MAX_DROPS;i++){
  const Drop *p=&g->drops[i];if(!p->active||p->kind==ITEM_GOLD||p->kind>=ITEM_COUNT)continue;
  int dx=(int)p->x-g->x,dy=(int)p->y-g->y;
  if(abs_i(dx)>1||abs_i(dy)>1||(!game_walkable(g,p->x,p->y)&&!game_quest_pickable(g,p)))continue;
  if(dx&&dy&&(!game_walkable(g,g->x+dx,g->y)||!game_walkable(g,g->x,g->y+dy)))continue;
  if(game_monster_blocks(g,p->x,p->y,-1))continue;
  int r=abs_i(direction(dx,dy)-g->facing);if(r>4)r=8-r;
  int n=(!dx&&!dy)?0:8+r*4+dx*dx+dy*dy;
  if(n<score){score=n;best=i;}
 }return best;
}
static int select_target(const Game *g,int manual,int retain){
    if(!manual && retain && game_target_visible(g,g->target))return g->target;
    int best=-1,score=100;
    for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){int i=(int)coop_enemy_index(g,n);if(g->enemies[i].alive==1&&reachable(g,i)){
        int d=direction(g->enemies[i].x-g->x,g->enemies[i].y-g->y);
        int rotation=abs_i(d-g->facing);if(rotation>4)rotation=8-rotation;
        if(manual && rotation)continue;
        if(rotation<score){best=i;score=rotation;}
    }
    }
    if(!manual&&game_spell_portal_target(g)>=0)return TARGET_PORTAL;
    if(best>=0||g->area)return best;
    /* A visible hostile takes precedence over nearby loot. Keep the same
     * visibility/corner contract as attacks; walls still permit item pickup. */
    best=game_ranged_target(g,manual);if(best>=0)return best;
    if(!manual&&game_item_target(g)>=0)return TARGET_ITEM;
    score=100;
    for(int i=0;i<MAX_PROPS;i++){
        const Prop *p=&g->props[i];if(!p->kind||game_prop_is_wall_trap(p->kind)||game_prop_is_theme_decor(p->kind)||p->kind==PROP_BRAZIER||(p->state&&!game_prop_repeatable(p->kind)))continue;
        /* Preserve sarcophagus reach and all four cells of a large fountain. */
        unsigned wide=game_prop_is_large_fountain(p->kind);
        if((unsigned)((int)p->x-g->x+1)>(wide?3u:2u)||(unsigned)((int)p->y-g->y+1)>((wide||p->kind==PROP_SARC)?3u:2u))continue;
        int d=game_prop_direction(g,i);if(d<0)continue;
        int rotation=abs_i(d-g->facing);if(rotation>4)rotation=8-rotation;
        if(manual&&rotation)continue;
        /* Nearby closed doors are the first object action. Keep enemies and
         * loot ahead of objects, and retain open-door closing on a fresh A.
         * The door-axis reach test above still applies; this never auto-opens. */
        if(!game_prop_is_door(p->kind)||p->state)rotation+=8;
        if(rotation<score){best=-i-2;score=rotation;}
    }
    return best;
}
/* Screen-clockwise eight directions in the isometric collision grid. */
static const int8_t step_x[8]={1,0,-1,-1,-1,0,1,1};
static const int8_t step_y[8]={1,1,1,0,-1,-1,-1,0};
static int free_cell(const Game *g,int x,int y){
    if(!game_walkable(g,x,y)||coop_movement_blocks(x,y,coop_turn))return 0;
    return !game_monster_blocks(g,x,y,-1);
}
static int can_move(const Game *g,int dir){
    int dx=step_x[dir],dy=step_y[dir];
#if PERF_BATCH_WALK
    if(!g->area&&dx&&dy){
     int x=g->x+dx,y=g->y+dy;
     if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H)return 0;
     uint16_t cells[3]={(uint16_t)(x|(y<<8)),(uint16_t)(x|(g->y<<8)),(uint16_t)(g->x|(y<<8))};
     if(!walk_batch_clear(g,cells,3))return 0;
     for(unsigned i=0;i<3;i++){int cx=cells[i]&255,cy=cells[i]>>8;if(coop_movement_blocks(cx,cy,coop_turn)||game_monster_blocks(g,cx,cy,-1))return 0;}
     return 1;
    }
#endif
    if(!free_cell(g,g->x+dx,g->y+dy))return 0;
    /* Retain the GBA strict corner rule, including blocking props. */
    return !dx||!dy||(free_cell(g,g->x+dx,g->y)&&free_cell(g,g->x,g->y+dy));
}
/* Only called when both slide candidates are legal. Prefer
 * stepping into an adjacent open doorway over continuing beside the wall.
 * Once inside a door, perpendicular wall-following must not bounce back to
 * another doorway behind it. No path search or persistent state. */
static int doorway_slide(const Game *g,int first,int second,int no_reverse){
    if(g->area)return first;
    int ax=g->x+step_x[first],ay=g->y+step_y[first];
    int bx=g->x+step_x[second],by=g->y+step_y[second],door_second=0;
    for(unsigned i=0;i<MAX_PROPS;i++){
        const Prop *p=&g->props[i];if(!p->state||!game_prop_is_door(p->kind))continue;
        if(no_reverse&&p->x==g->x&&p->y==g->y)return first;
        if(p->x==ax&&p->y==ay)return first;
        if(p->x==bx&&p->y==by)door_second=1;
    }
    return door_second?second:first;
}
/* One deliberate reversal may enter a door behind a blocked wall slide.
 * Never bounce back from a doorway: adjacent doors otherwise form a loop. */
static int doorway_reverse(const Game *g,int dir){
    if(g->area)return 0;
    int x=g->x+step_x[dir],y=g->y+step_y[dir],found=0;
    for(unsigned i=0;i<MAX_PROPS;i++){
        const Prop *p=&g->props[i];if(!p->state||!game_prop_is_door(p->kind))continue;
        if(p->x==g->x&&p->y==g->y)return 0;
        if(p->x==x&&p->y==y)found=1;
    }
    return found;
}
static int slide_step(const Game *g,int requested,int previous,int turn){
    int first=(requested+turn)&7,second=(requested+8-turn)&7;
    if(previous==second){int t=first;first=second;second=t;}
    int reverse=turn==2&&previous==first;
    if(can_move(g,first))return can_move(g,second)?doorway_slide(g,first,second,reverse):first;
    if(can_move(g,second)&&(!reverse||doorway_reverse(g,second)))return second;
    return -1;
}
/* Search once per completed walk, with bounded stack storage. First-step
 * labels avoid a parent/path allocation. Occupancy includes moving monsters. */
static int chase_step(const Game *g,int target){
 /* Equal-length paths are explored in target-heading order below.
  * Keep the full search: greedy steps can oscillate around another actor. */
 uint8_t first[289];uint16_t queue[289];int ox=g->x-8,oy=g->y-8;
#ifdef GAME_NO_COLLISION_WINDOW
 for(int y=0;y<17;y++)for(int x=0;x<17;x++)first[y*17+x]=game_walkable(g,ox+x,oy+y)?254:255;
#else
 game_chase_collision_fill(g,ox,oy,first);
#endif
 for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){int i=(int)coop_enemy_index(g,n);if(g->enemies[i].alive==1){
  int x=g->enemies[i].x-ox,y=g->enemies[i].y-oy;
  if(x>=0&&y>=0&&x<17&&y<17)first[y*17+x]=255;
 }
 }
 int tx=g->enemies[target].x,ty=g->enemies[target].y;
 unsigned head=0,tail=1;queue[0]=144;first[144]=8;
 while(head<tail){int p=queue[head++],x=p%17,y=p/17,dx=tx-ox-x,dy=ty-oy-y;
  if(abs_i(dx)<=1&&abs_i(dy)<=1&&(!dx||!dy||(game_walkable(g,ox+x+dx,oy+y)&&game_walkable(g,ox+x,oy+y+dy))))return first[p]<8?first[p]:-1;
  static const int8_t turns[8]={0,1,-1,2,-2,3,-3,4};
  int heading=direction(dx,dy);
  for(int k=0;k<8;k++){int d=(heading+turns[k]+8)&7;int nx=x+step_x[d],ny=y+step_y[d],n=ny*17+nx;
   if(nx<0||ny<0||nx>=17||ny>=17||first[n]!=254)continue;
   if(step_x[d]&&step_y[d]&&(first[y*17+nx]==255||first[ny*17+x]==255))continue;
   if(p==144&&!can_move(g,d))continue;
   first[n]=p==144?d:first[p];queue[tail++]=n;
  }
 }return -1;
}
/* One bounded breadth-first search per directional input, shared by all
 * melee candidates. Same17x17 collision window as pursuit, max25 steps.
 * Occupied cells and blocked corners cannot form a shorter fictitious path. */
static __attribute__((section(".text.ui"),noinline)) void aim_path_distances(const Game *g,uint8_t distance[289]){
 int ox=g->x-8,oy=g->y-8;
#ifdef GAME_NO_COLLISION_WINDOW
 for(int y=0;y<17;y++)for(int x=0;x<17;x++)distance[y*17+x]=game_walkable(g,ox+x,oy+y)?254:255;
#else
 game_chase_collision_fill(g,ox,oy,distance);
#endif
 for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){
  const Enemy *e=&g->enemies[coop_enemy_index(g,n)];
  int x=e->x-ox,y=e->y-oy;
  if(e->alive==1&&(unsigned)x<17&&(unsigned)y<17)distance[y*17+x]=255;
 }
 for(int y=0;y<17;y++)for(int x=0;x<17;x++)
  if(coop_movement_blocks(ox+x,oy+y,coop_turn))distance[y*17+x]=255;
 uint16_t queue[289];unsigned head=0,tail=1;queue[0]=144;distance[144]=0;
 while(head<tail){
  unsigned p=queue[head++];int x=p%17,y=p/17;
  if(distance[p]>=24)continue;
  for(unsigned d=0;d<8;d++){
   int nx=x+step_x[d],ny=y+step_y[d],at=ny*17+nx;
   if((unsigned)nx>=17||(unsigned)ny>=17||distance[at]!=254)continue;
   if(step_x[d]&&step_y[d]&&(distance[y*17+nx]==255||distance[ny*17+x]==255))continue;
   distance[at]=distance[p]+1;queue[tail++]=(uint16_t)at;
  }
 }
}
static __attribute__((section(".text.ui"),noinline)) int aim_melee_distance(const Game *g,const Enemy *e,const uint8_t distance[289]){
 int best=26;
 for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
  if(!dx&&!dy)continue;
  int x=e->x+dx,y=e->y+dy,wx=x-g->x+8,wy=y-g->y+8;
  if((unsigned)wx>=17||(unsigned)wy>=17)continue;
  if(dx&&dy&&(!game_walkable(g,x,e->y)||!game_walkable(g,e->x,y)))continue;
  int steps=distance[wy*17+wx]+1;if(steps<best)best=steps;
 }
 return best;
}
/* Directional intent replaces the retained enemy, without rotating or
 * cancelling the committed swing. Visibility rejects walls/dead actors. */
static __attribute__((section(".text.ui"),noinline)) void aim_held_attack(Game *g,unsigned held,unsigned pressed){
 const unsigned arrows=KEY_RIGHT|KEY_LEFT|KEY_UP|KEY_DOWN;
 if(g->area||!(held&KEY_A)||!(pressed&(arrows|KEY_A)))return;
 int sx=!!(held&KEY_RIGHT)-!!(held&KEY_LEFT),sy=!!(held&KEY_DOWN)-!!(held&KEY_UP);
 if(!sx&&!sy)return;
 int heading=direction(sign(sx+sy),sign(sy-sx)),best=-1,score=100000;
 uint8_t distance[289];int ranged=game_uses_bow();
 if(!ranged)aim_path_distances(g,distance);
 for(unsigned n=0,total=coop_enemy_total(g);n<total;n++){
  int i=(int)coop_enemy_index(g,n);
  if(!game_target_visible(g,i))continue;
  int dx=g->enemies[i].x-g->x,dy=g->enemies[i].y-g->y;
  int angle=abs_i(direction(dx,dy)-heading);if(angle>4)angle=8-angle;
  if(angle>1)continue;
  int steps=ranged?dx*dx+dy*dy:aim_melee_distance(g,&g->enemies[i],distance);
  if(!ranged&&steps>25)continue;
  int rank=steps*8+angle;
  if(rank<score){score=rank;best=i;}
 }
 if(best>=0){g->target=(int8_t)best;if(g->attack_queued==2)g->attack_queued=0;}
}
int game_controller_step(Game *g,unsigned held,unsigned pressed){
    aim_held_attack(g,held,pressed);
    /* One physical A hold owns its monster. Death / lost visibility consumes
     * that hold before selection can acquire a replacement, even during hit
     * recovery. Keep swing_target and attack_wait: the committed swing ends
     * normally. State2 already survives reactions and co-op capture/restore. */
    if((!(held&KEY_A)||(pressed&KEY_A))&&g->attack_queued==2)g->attack_queued=0;
    if((held&KEY_A)&&g->target>=0&&!game_target_visible(g,g->target)){
        if(!(pressed&KEY_A))g->attack_queued=2;
        g->target=-1;
    }
    /* B casts the selected quick slot, once per press. It no longer holds aim. */
    if(pressed&KEY_B){game_spell_request_selected(g);game_spell_control(g);return -1;}
    held&=~KEY_B;pressed&=~KEY_B;
    if(game_spell_control(g))return -1;
    if(g->hit_flash&&!g->area){if(g->attack_queued!=2)g->attack_queued=0;return -1;}
    /* A tap during a walk survives release until the current step finishes.
     * Presses during a swing do not queue an extra attack after release. */
    /* Object use or an invalidated monster consumes A until release/repress. */
    if(g->attack_queued==2){held&=~KEY_A;pressed&=~KEY_A;}
    if((pressed&KEY_A) && !g->attack_wait)g->attack_queued=1;

    if(g->attack_wait){
        unsigned impact=game_attack_hit_wait(game_hero_class(),g->attack_duration);
        if(g->attack_wait==impact){
            if(game_attack_bow(g->attack_duration)){game_bow_fire(g);return -1;}
            if(g->swing_target<=-2&&g->swing_target>=-65){int i=-g->swing_target-2;
                if(game_prop_is_barrel(g->props[i].kind)&&game_prop_direction(g,i)==g->facing)return g->swing_target;
            }else if(reachable(g,g->swing_target)&&direction(g->enemies[g->swing_target].x-g->x,g->enemies[g->swing_target].y-g->y)==g->facing)return g->swing_target;
        }
        /* A hit is not the end of an attack. Both tapped and held A finish
         * the complete, speed-adjusted cycle before another attack can start.
         * This is deliberately slower than native early-repeat CheckNewPath. */
        return -1;
    }
    if(g->move_wait){g->target=(int8_t)select_target(g,0,(held&KEY_A)!=0);return -1;}
    int sx=((held&KEY_RIGHT)!=0)-((held&KEY_LEFT)!=0);
    int sy=((held&KEY_DOWN)!=0)-((held&KEY_UP)!=0);
    int dx=sign(sx+sy),dy=sign(sy-sx);
    int previous=g->facing;
    if(dx||dy)g->facing=(uint8_t)direction(dx,dy);
    g->target=(int8_t)select_target(g,(held&KEY_B)!=0,(held&KEY_A)!=0);
    if((held&KEY_A) || g->attack_queued==1){
        int requested=(pressed&KEY_A)||g->attack_queued==1;
        g->attack_queued=0;
        /* Selection is read-only and does not inspect attack_queued. Reuse the
         * target selected above, including retained targets and queued taps. */
        if(g->target==TARGET_PORTAL){g->swing_target=-1;return -1;}
        if(g->target==TARGET_ITEM){
            /* Loot needs a new A press, never the held tail of a combat swing.
             * A tap queued while walking executes once the step finishes. */
            if(requested){game_item_pickup(g,game_item_target(g));g->attack_queued=2;}
            g->swing_target=-1;return -1;
        }
        if(!game_uses_bow()&&g->target>=0&&!reachable(g,g->target)){
            g->swing_target=-1;
            if(!(held&KEY_B)){
                int d=chase_step(g,g->target);
                if(d>=0){g->x+=step_x[d];g->y+=step_y[d];g->facing=d;g->move_wait=WALK_TICKS;}
            }
            return -1;
        }
        g->swing_target=g->target;
        if(g->target>=0)g->facing=(uint8_t)direction(g->enemies[g->target].x-g->x,g->enemies[g->target].y-g->y);
        if(g->target<=-2){
            int i=-g->target-2;g->facing=(uint8_t)game_prop_direction(g,i);
            if(!game_prop_is_barrel(g->props[i].kind)){if(requested){game_operate_prop(g,i);g->attack_queued=2;}g->swing_target=-1;return -1;}
        }
        game_attack_repeat[coop_active?coop_turn:0]=0;
        g->attack_duration=game_attack_duration(game_uses_bow());if(game_uses_bow())game_bow_aim(g);g->attack_wait=game_attack_ticks(g->attack_duration);
        return -1;
    }
    if((held&KEY_B) || !(dx||dy))return -1;
    int chosen=direction(dx,dy);
    if(!can_move(g,chosen)){
        int requested=chosen;chosen=slide_step(g,requested,previous,1);
        /* Screen diagonals are world-axis moves. Their two 45-degree steps
         * can both fail the strict corner rule, so try two wall-parallel
         * 90-degree steps. Keep that direction; stop rather than oscillate. */
        if(chosen<0&&(requested&1))chosen=slide_step(g,requested,previous,2);
        if(chosen<0){
            if((requested&1)&&(previous==((requested+2)&7)||previous==((requested+6)&7)))g->facing=(uint8_t)previous;
            return -1;
        }
    }
    g->x+=step_x[chosen];g->y+=step_y[chosen];
    g->facing=(uint8_t)chosen;g->move_wait=WALK_TICKS;
    return -1;
}
