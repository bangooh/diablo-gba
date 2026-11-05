/* Reachability mask lives AFTER room IDs and actor staging. Include terrain
 * seals as well as ordinary doors. Infravision bypasses rejection in draw().
 * Flood scratch is borrowed only on rebuild, with raw-cache tags retired. */
static volatile unsigned closed_door_cull_enabled=1;
static volatile unsigned closed_door_skipped;
#define CD_CLOSED ((uint8_t*)town_map+GAME_ROOMS_LIVE_OFFSET+GAME_ROOMS_LIVE_BYTES)
_Static_assert(GAME_ROOMS_LIVE_OFFSET+GAME_ROOMS_LIVE_BYTES+800<=sizeof(town_map),"visibility mask exceeds arena");
static inline unsigned closed_door_bit(const uint8_t *bits,unsigned cell){return bits[cell>>3]&(1u<<(cell&7));}
static UI_FN unsigned closed_door_tile(unsigned cell){
 unsigned p=PROP_LOOKUP[cell];return p&&game_prop_is_door(game.props[p-1].kind)&&!game.props[p-1].state;
}
static UI_FN __attribute__((target("thumb"),minsize)) void closed_door_flood(uint8_t *bits){
 for(unsigned c=0;c<800;c++)bits[c]=0;
 uint16_t *queue=(uint16_t*)load_buffer;unsigned head=0,tail=0,start=game.y*80+game.x;
 queue[tail++]=(uint16_t)start;bits[start>>3]|=1u<<(start&7);
 while(head<tail){unsigned a=queue[head++],x=a%80;
  int next[4]={(int)a-80,(int)a+80,x?(int)a-1:-1,x<79?(int)a+1:-1};
  for(unsigned d=0;d<4;d++){unsigned n=(unsigned)next[d];if(n>=6400||closed_door_bit(bits,n)||!game.map[n/80][n%80]||closed_door_tile(n))continue;
   bits[n>>3]|=1u<<(n&7);queue[tail++]=(uint16_t)n;
  }
 }
}
static UI_FN void closed_door_fallback(void){
 if(game.area||!closed_door_cull_enabled)return;
 if(closed_door_cull_valid&&closed_door_bit(CD_CLOSED,game.y*80+game.x))return;
 closed_door_cull_valid=1;
 for(unsigned i=0;i<4;i++)raw_base[i]=0;
 closed_door_flood(CD_CLOSED);
}
static UI_FN unsigned closed_door_fallback_hidden(unsigned i){
 if(game.area||!closed_door_cull_enabled||!closed_door_cull_valid)return 0;
 const Enemy *e=&game.enemies[i];unsigned cell=e->y*80+e->x;
 /* Fail open on non-floor actor positions, matching an unlabeled graph tile. */
 return game.map[game.y][game.x]&&game.map[e->y][e->x]&&!closed_door_bit(CD_CLOSED,cell)&&!closed_door_tile(cell);
}
