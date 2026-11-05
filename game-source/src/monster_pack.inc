/* Retail GroupUnity / DirOK pack cohesion. Included by monster_ai.c after
 * seen/last_x/last_y and AI_ROM are defined. Normal PlaceGroup(...,0,0)
 * clusters are NOT leader packs. Four explicit native leader/minion kind pairs
 * retain membership across floor/save restores; never infer membership from
 * current proximity.
 * Enabled storage: one byte per actor, no per-leader counters. */
#ifndef MONSTER_LEADER_PACKS
#define MONSTER_LEADER_PACKS 1
#endif
#if MONSTER_LEADER_PACKS
_Static_assert(MAX_ENEMIES < 127,"seven-bit pack leader slot");
static uint8_t pack_link[MAX_ENEMIES];
enum { PACK_DETACHED=128,PACK_SLOT=127 };
static int pack_abs(int n){return n<0?-n:n;}
static int pack_near(int x,int y,const Enemy *e){return pack_abs(x-e->x)<4&&pack_abs(y-e->y)<4;}
static int pack_solid_free(const Game *g,int x,int y){
 return (unsigned)x<MAP_W&&(unsigned)y<MAP_H&&g->map[y][x];
}
/* Keep the original LineClearF rounding and endpoint rule. GroupUnity uses
 * CheckNoSolid, which ignores actors and blocking props. */
static int pack_line_clear(const Game *g,int x1,int y1,int x2,int y2){
 int xorg=x1,yorg=y1,dx=x2-x1,dy=y2-y1,d,inc,diag,step,tmp,done=0;
 if(pack_abs(dx)>pack_abs(dy)){
  if(dx<0){tmp=x1;x1=x2;x2=tmp;tmp=y1;y1=y2;y2=tmp;dx=-dx;dy=-dy;}
  if(dy>0){d=2*dy-dx;inc=2*dy;diag=2*(dy-dx);step=1;}
  else {d=2*dy+dx;inc=2*dy;diag=2*(dx+dy);step=-1;}
  while(!done&&(x1!=x2||y1!=y2)){
   if((d<=0)^(step<0))d+=inc;else {d+=diag;y1+=step;}
   ++x1;done=(x1!=xorg||y1!=yorg)&&!pack_solid_free(g,x1,y1);
  }
 }else{
  if(dy<0){tmp=y1;y1=y2;y2=tmp;tmp=x1;x1=x2;x2=tmp;dy=-dy;dx=-dx;}
  if(dx>0){d=2*dx-dy;inc=2*dx;diag=2*(dx-dy);step=1;}
  else {d=2*dx+dy;inc=2*dx;diag=2*(dy+dx);step=-1;}
  while(!done&&(y1!=y2||x1!=x2)){
   if((d<=0)^(step<0))d+=inc;else {d+=diag;x1+=step;}
   ++y1;done=(y1!=yorg||x1!=xorg)&&!pack_solid_free(g,x1,y1);
  }
 }
 return x1==x2&&y1==y2;
}
AI_ROM void game_pack_clear(void){for(unsigned i=0;i<MAX_ENEMIES;i++)pack_link[i]=0;}
AI_ROM void game_pack_forget(unsigned i){
 if(i>=MAX_ENEMIES)return;
 pack_link[i]=0;
 for(unsigned j=0;j<MAX_ENEMIES;j++)if((pack_link[j]&PACK_SLOT)==i+1)pack_link[j]=0;
}
AI_ROM int game_pack_assign(const Game *g,unsigned member,unsigned leader){
 if(member>=MAX_ENEMIES||leader>=MAX_ENEMIES||member==leader||!g->enemies[member].alive||!g->enemies[leader].alive||pack_link[leader])return 0;
 /* Authored native packs are one level deep. Reject cycles/nested packs. */
 for(unsigned j=0;j<MAX_ENEMIES;j++)if((pack_link[j]&PACK_SLOT)==member+1)return 0;
 pack_link[member]=(uint8_t)(leader+1);return 1;
}
AI_ROM uint32_t game_pack_hash(uint32_t h){for(unsigned i=0;i<MAX_ENEMIES;i++)h=(h^pack_link[i])*16777619u;return h;}
AI_ROM void game_pack_unity(const Game *g,unsigned i){
 if(i>=MAX_ENEMIES)return;
 if(!g->enemies[i].alive){game_pack_forget(i);return;}
 unsigned link=pack_link[i],leader=(link&PACK_SLOT)-1;
 if(link){
  if(leader>=MAX_ENEMIES||!g->enemies[leader].alive){game_pack_forget(i);return;}
  const Enemy *e=&g->enemies[i],*l=&g->enemies[leader];
  int clear=pack_line_clear(g,e->x,e->y,l->x,l->y);
  if(!clear)pack_link[i]|=PACK_DETACHED;
  else if((link&PACK_DETACHED)&&pack_near(e->x,e->y,l))pack_link[i]&=PACK_SLOT;
  if(!(pack_link[i]&PACK_DETACHED)&&seen[i]>seen[leader]){
   last_x[leader]=e->x;last_y[leader]=e->y;seen[leader]=seen[i]-1;
  }
 }else{
  /* A registered root is the native unique leader; unrelated actors have
   * no matching links and cannot share awareness. */
  for(unsigned j=0;j<MAX_ENEMIES;j++)if(pack_link[j]==i+1&&g->enemies[j].alive&&seen[i]>seen[j]){
   last_x[j]=g->enemies[i].x;last_y[j]=g->enemies[i].y;seen[j]=seen[i]-1;
  }
 }
}
AI_ROM int game_pack_step_allowed(const Game *g,unsigned i,int x,int y){
 if(i>=MAX_ENEMIES)return 0;
 unsigned link=pack_link[i],leader=(link&PACK_SLOT)-1;
 if(link&&!(link&PACK_DETACHED)&&leader<MAX_ENEMIES&&g->enemies[leader].alive)return pack_near(x,y,&g->enemies[leader]);
 if(link)return 1;
 for(unsigned j=0;j<MAX_ENEMIES;j++)if(pack_link[j]==i+1&&g->enemies[j].alive&&!pack_near(x,y,&g->enemies[j]))return 0;
 return 1;
}
#else
/* Explicit test/build opt-out: no pack state allocation. */
void game_pack_clear(void){}
void game_pack_forget(unsigned i){(void)i;}
int game_pack_assign(const Game *g,unsigned member,unsigned leader){(void)g;(void)member;(void)leader;return 0;}
static inline uint32_t game_pack_hash(uint32_t h){return h;}
static inline void game_pack_unity(const Game *g,unsigned i){(void)g;(void)i;}
static inline int game_pack_step_allowed(const Game *g,unsigned i,int x,int y){(void)g;(void)i;(void)x;(void)y;return 1;}
#endif
