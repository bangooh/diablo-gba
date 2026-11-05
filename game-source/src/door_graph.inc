/* Compact RLE tile->component map, 64 door edges and one 256-bit reach set.
 * All persistent data fits the 2368-byte tail AFTER native room IDs. */
#define DG_ROWS ((uint16_t*)CD_CLOSED)
#define DG_RUNS (CD_CLOSED+162)
#define DG_EDGES (DG_RUNS+1800)
#define DG_CLOSED (DG_EDGES+320)
_Static_assert(GAME_ROOMS_LIVE_OFFSET+GAME_ROOMS_LIVE_BYTES+2314<=sizeof(town_map),"door graph exceeds arena");
static unsigned door_graph_origin;
static UI_FN unsigned door_graph_at(unsigned x,unsigned y){
 if(x>=80||y>=80)return 0;
 for(unsigned r=DG_ROWS[y];r<DG_ROWS[y+1];r++)if(x<DG_RUNS[r*2])return DG_RUNS[r*2+1];
 return 0;
}
static UI_FN __attribute__((target("thumb"),minsize)) void door_graph_build(void){
 door_graph_state=2;closed_door_cull_valid=0;
 if(game.area)return;
 uint8_t *labels=(uint8_t*)load_buffer;uint16_t *queue=(uint16_t*)(labels+6400);
 for(unsigned i=0;i<4;i++)raw_base[i]=0;
 for(unsigned c=0;c<6400;c++)labels[c]=0;
 unsigned group=0;
 for(unsigned c=0;c<6400;c++){
  unsigned p=PROP_LOOKUP[c];
  if(labels[c]||!game.map[c/80][c%80]||(p&&game_prop_is_door(game.props[p-1].kind)))continue;
  if(++group==256)return;
  unsigned head=0,tail=0;queue[tail++]=(uint16_t)c;labels[c]=(uint8_t)group;
  while(head<tail){unsigned a=queue[head++],x=a%80;
   int ns[4]={(int)a-80,(int)a+80,x?(int)a-1:-1,x<79?(int)a+1:-1};
   for(unsigned d=0;d<4;d++){unsigned n=(unsigned)ns[d];if(n>=6400||labels[n]||!game.map[n/80][n%80])continue;
    unsigned prop=PROP_LOOKUP[n];if(prop&&game_prop_is_door(game.props[prop-1].kind))continue;
    if(tail>=6144)return;labels[n]=(uint8_t)group;queue[tail++]=(uint16_t)n;
   }
  }
 }
 for(unsigned i=0;i<MAX_PROPS;i++)if(game_prop_is_door(game.props[i].kind)){
  if(++group==256)return;labels[game.props[i].y*80+game.props[i].x]=(uint8_t)group;
 }
 unsigned runs=0;
 for(unsigned y=0;y<80;y++){
  DG_ROWS[y]=(uint16_t)runs;
  for(unsigned x=0;x<80;){unsigned label=labels[y*80+x];while(x<80&&labels[y*80+x]==label)x++;
   if(runs>=900)return;DG_RUNS[runs*2]=(uint8_t)x;DG_RUNS[runs*2+1]=(uint8_t)label;runs++;
  }
 }
 DG_ROWS[80]=(uint16_t)runs;
 for(unsigned i=0;i<MAX_PROPS;i++){
  uint8_t *edge=DG_EDGES+i*5;for(unsigned j=0;j<5;j++)edge[j]=0;
  const Prop *p=&game.props[i];if(!game_prop_is_door(p->kind))continue;
  unsigned cell=p->y*80+p->x;edge[0]=labels[cell];
  int ns[4]={(int)cell-80,(int)cell+80,p->x?(int)cell-1:-1,p->x<79?(int)cell+1:-1};
  for(unsigned j=0;j<4;j++)if((unsigned)ns[j]<6400)edge[j+1]=labels[ns[j]];
 }
 door_graph_state=1;
}
static UI_FN void door_graph_reach(uint8_t *bits,unsigned from){
 for(unsigned i=0;i<32;i++)bits[i]=0;
 if(!from)return;bits[from>>3]|=1u<<(from&7);
 unsigned changed;
 do{changed=0;for(unsigned i=0;i<MAX_PROPS;i++){
  const uint8_t *edge=DG_EDGES+i*5;if(!edge[0]||!game.props[i].state)continue;
  unsigned hit=0;for(unsigned j=0;j<5;j++)if(edge[j]&&closed_door_bit(bits,edge[j]))hit=1;
  if(hit)for(unsigned j=0;j<5;j++){unsigned n=edge[j];if(n&&!closed_door_bit(bits,n)){bits[n>>3]|=1u<<(n&7);changed=1;}}
 }}while(changed);
}
static UI_FN void closed_door_prepare(void){
 if(game.area||!closed_door_cull_enabled)return;
 if(!door_graph_state)door_graph_build();
 if(door_graph_state!=1){closed_door_fallback();return;}
 unsigned from=door_graph_at(game.x,game.y);
 if(closed_door_cull_valid&&from==door_graph_origin)return;
 door_graph_origin=from;door_graph_reach(DG_CLOSED,from);closed_door_cull_valid=1;
}
static UI_FN unsigned closed_door_monster_hidden(unsigned i){
 if(game.area||!closed_door_cull_enabled)return 0;
 if(door_graph_state!=1)return closed_door_fallback_hidden(i);
 const Enemy *e=&game.enemies[i];unsigned to=door_graph_at(e->x,e->y);
 /* Unreachable components include quest seals. Never infer line of sight:
  * a room connected by another open route retains its previous rendering. */
 return door_graph_origin&&to&&!closed_door_bit(DG_CLOSED,to)&&!closed_door_tile(e->y*80+e->x);
}
