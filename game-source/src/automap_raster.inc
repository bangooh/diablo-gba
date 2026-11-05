/* Nominal200% world geometry, one-pixel contours. The512x256 BG2 contains a
 * sliding384x224 cache (320x208 compact retry). Ordinary camera motion changes
 * only hardware scroll. Rebuilds occur at discovery or near the cache edge.
 * Coherent integer isometric vertices guarantee shared1:2 diagonal endpoints.
 * These97x97 bitsets and all staging buffers reuse the existing caches. */
enum { MAP_GRID=97, MAP_EDGE_BYTES=(97*97+7)/8 };
_Static_assert(MAP_EDGE_BYTES<=1800,"map edge cache capacity");
/* Both edge bitsets stop at1177 bytes. The saved fog occupies an aligned,
 * otherwise unused tail of the1800-byte perpendicular-edge allocation. */
#define MAP_OLD_FOG (MAP_REVEALED+1180)
_Static_assert(MAP_EDGE_BYTES<=1180&&1180+200<=1800,"map saved fog tail");
#ifndef MAP_VRAM_WORD
#define MAP_VRAM_WORD(address) (*(const volatile uint32_t*)(address))
#endif
static inline unsigned map_edge(const uint8_t *bits,unsigned x,unsigned y){
    unsigned n=y*MAP_GRID+x;return (bits[n>>3]>>(n&7))&1;
}
static UI_FN void automap_build_outline(void){
    uint8_t walk[96*96/8];
    for(unsigned i=0;i<sizeof(walk);i++)walk[i]=0;
    for(unsigned y=0;y<96;y++)for(unsigned x=0;x<96;x++){
        unsigned open=game.area?town_walkable((int)x,(int)y):(x<80&&y<80&&game.map[y][x]);
        unsigned n=y*96+x;if(open)walk[n>>3]|=(uint8_t)(1u<<(n&7));
    }
    /* Mark only the actual door cell as a passage in either door state.
     * Widening into neighboring cells erases walls and makes the room look
     * open. Keep those cells intact so the jambs bound the real door width. */
    if(!game.area)for(unsigned i=0;i<MAX_PROPS;i++){
        const Prop *p=&game.props[i];if(!game_prop_is_door(p->kind)||p->x>=80||p->y>=80)continue;
        unsigned n=p->y*96+p->x;walk[n>>3]|=(uint8_t)(1u<<(n&7));
    }
    for(unsigned i=0;i<MAP_EDGE_BYTES;i++)MAP_OUTLINE[i]=MAP_REVEALED[i]=0;
    for(unsigned y=0;y<MAP_GRID;y++)for(unsigned x=0;x<MAP_GRID;x++){
        unsigned n=y*MAP_GRID+x,cell=y*96+x;
        unsigned here=x<96&&y<96?((walk[cell>>3]>>(cell&7))&1):0;
        unsigned above=x<96&&y?((walk[(cell-96)>>3]>>((cell-96)&7))&1):0;
        unsigned left=x&&y<96?((walk[(cell-1)>>3]>>((cell-1)&7))&1):0;
        if(here!=above)MAP_OUTLINE[n>>3]|=(uint8_t)(1u<<(n&7));
        if(here!=left){unsigned v=x*MAP_GRID+y;MAP_REVEALED[v>>3]|=(uint8_t)(1u<<(v&7));}
    }
    /* Collision contours can still put a crossbar on either lip of a
     * narrow doorway. Remove only the two edges crossed by its travel axis;
     * retain the side jambs. Cached and streaming maps share these bitsets. */
    if(!game.area)for(unsigned i=0;i<MAX_PROPS;i++){
        const Prop *p=&game.props[i];if(!game_prop_is_door(p->kind)||p->x>=80||p->y>=80)continue;
        unsigned axis=game_prop_door_axis(p->kind);
        uint8_t *edges=axis?MAP_OUTLINE:MAP_REVEALED;
        unsigned n=(axis?p->y:p->x)*MAP_GRID+(axis?p->x:p->y);
        for(unsigned side=0;side<2;side++,n+=MAP_GRID)edges[n>>3]&=(uint8_t)~(1u<<(n&7));
    }
    map_contour_builds++;
}
