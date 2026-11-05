/* Deliberately independent world-edge oracle and persistent VRAM sequences.
 * Production is included without replacing its raster/packing/state logic. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#define UI_FN
static Game game;
uint8_t town_collision[1152];
static uint8_t outline[1800],revealed[1800],fog_bits[200],oracle[512][1024];
static struct {uint32_t before[8],buffer[4672],after[8];} scratch_guard;
#define load_buffer scratch_guard.buffer
#define MAP_OUTLINE outline
#define MAP_REVEALED revealed
static const uint8_t *raw_base[4];
static unsigned map_contour_builds,map_world_fallbacks,map_unique_tiles,map_tile_overflows;
static unsigned map_commits,map_commit_overruns;
static uint8_t map_screen_fallback,map_screen_live;
static unsigned map_visible=1,map_init,map_last_revision,map_pending,map_line_color=0x37,revision=1;
static int map_last_x=-1,map_last_y=-1,map_last_area=-1,map_ref_x,map_ref_y,cam_x,cam_y;
static uint32_t io[256/4],vram[65536/4];
static uint8_t *physical(uintptr_t at,unsigned bytes){
    if(at>=0x04000000&&at+bytes<=0x04000100)return (uint8_t*)io+at-0x04000000;
    assert(at>=0x06000000&&at+bytes<=0x06010000);return (uint8_t*)vram+at-0x06000000;
}
#define R16(a) (*(uint16_t*)physical((a),2))
#define MAP_VRAM_WORD(a) (*(const uint32_t*)physical((a),4))
static unsigned dma_writes,dma_reads;
static void dma(const void *src,void *dst,unsigned count){
    uintptr_t s=(uintptr_t)src,d=(uintptr_t)dst;
    if(s>=0x06000000&&s<0x06010000){src=physical(s,count);dma_reads++;}
    if(d>=0x06000000&&d<0x06010000){dst=physical(d,count);dma_writes++;}
    memcpy(dst,src,count);
}
void game_explore_export(uint8_t *out){memcpy(out,fog_bits,200);}
unsigned game_explore_revision(void){return revision;}
static inline unsigned outer_map_tile(unsigned n){return n<174?530+n:n<238?896+n-174:n<282?266+n-238:326+n-282;}
#include "candidate-config.inc"
#include "automap_raster.inc"
#define OUTER_MAP_BG2_CONTROL 0x1d09
#include "candidate-commit.inc"
QuestProgress quest_progress;
uint16_t quest_anvil_cell,quest_valor_cell,quest_banner_cell,quest_king_cell,quest_water_cell;
unsigned coop_active,coop_campaign_active;
static uint8_t scratch[159744];uint8_t *dungeon_workspace=scratch;
const uint8_t asset_world_l1[1]={0},asset_world_l2[1]={0},asset_world_l3[1]={0},asset_world_l4[1]={0};
#include "world.c"
int game_quest_valor_room(int x,int y){(void)x;(void)y;return 0;}
#include "census-dungeon.inc"

static const char *label="init";
static unsigned scenes,frames,partial_updates,no_pixel_updates,full_fallbacks,recenters,cancellations;
static unsigned explicit_decode_cases,mode_frames[3],flips_seen,branch_cells,repeat_checks;
static unsigned capacity_rejections,menu_restores,threshold_rejections,native_scenes;
static uint64_t pixels_checked;
static uint32_t random_state=78123;
static unsigned rng(void){random_state=random_state*1664525u+1013904223u;return random_state>>8;}
static int fixed_vertex(int n){assert(n>=0);return (n*16+4)/9;}
static int world_x(int x,int y){return 2*(fixed_vertex(x)-fixed_vertex(y));}
static int world_y(int x,int y){return fixed_vertex(x)+fixed_vertex(y);}
static int independent_walk(int x,int y){
    if(x<0||y<0||x>=96||y>=96)return 0;
    if(game.area){unsigned n=(unsigned)y*96+x;return (town_collision[n>>3]>>(n&7))&1;}
    return x<80&&y<80&&game.map[y][x]!=0;
}
static int independent_seen(int x,int y){
    if(game.area)return 1;
    if(x<0||y<0||x>=80||y>=80)return 0;
    unsigned cell=(unsigned)y/2*40+(unsigned)x/2;return (fog_bits[cell/8]>>(cell%8))&1;
}
static void oracle_line(int ax,int ay,int bx,int by){
    int x1=world_x(ax,ay),y1=world_y(ax,ay)-2,x2=world_x(bx,by),y2=world_y(bx,by)-2;
    int start=x1<x2?x1:x2,end=x1>x2?x1:x2;
    for(int x=start;x<=end;x++){
        double exact=y1+(double)(x-x1)*(y2-y1)/(x2-x1);
        int y=(int)exact;if(y>exact)y--;
        assert((unsigned)(x+512)<1024&&(unsigned)(y+64)<512);oracle[y+64][x+512]=1;
    }
}
static void make_oracle(void){
    memset(oracle,0,sizeof oracle);
    for(int y=0;y<=96;y++)for(int x=0;x<=96;x++){
        if(x<96&&independent_walk(x,y)!=independent_walk(x,y-1)&&
           (independent_seen(x,y)||independent_seen(x,y-1)))oracle_line(x,y,x+1,y);
        if(y<96&&independent_walk(x,y)!=independent_walk(x-1,y)&&
           (independent_seen(x,y)||independent_seen(x-1,y)))oracle_line(x,y,x,y+1);
    }
}
static void canaries(void){
    for(unsigned i=0;i<8;i++)assert(scratch_guard.before[i]==0xa5a5a5a5&&scratch_guard.after[i]==0xa5a5a5a5);
    /* None of the three atlas layouts owns the BG3 mask or low world VRAM. */
    const uint8_t *v=(const uint8_t*)vram;
    for(unsigned i=0;i<0xa140;i++)assert(v[i]==0xcc);
    for(unsigned i=0xf800;i<65536;i++)assert(v[i]==0xcc);
    for(unsigned i=1380;i<sizeof revealed;i++)assert(revealed[i]==0xa6);
}
static void check_pixels(void){
    assert(!map_pending&&!map_init&&map_last_x>=0);make_oracle();
    unsigned mode=map_screen_live,width=(mode&1)?256:512;
    int ox=(mode&1)?120-world_x(game.x,game.y):256-((world_x(map_last_x,map_last_y)+4)&~7);
    int oy=(mode&1)?80-world_y(game.x,game.y):128-((world_y(map_last_x,map_last_y)+4)&~7);
    unsigned left=(mode&1)?0:mode==2?96:64,right=(mode&1)?240:mode==2?416:448;
    unsigned top=(mode&1)?0:mode==2?24:16,bottom=(mode&1)?160:mode==2?232:240;
    const uint8_t *v=(const uint8_t*)vram;
    for(unsigned y=0;y<256;y++)for(unsigned x=0;x<width;x++){
        unsigned tx=x/8,ty=y/8,entry=((const uint16_t*)v)[0xe800/2+(tx>>5)*1024+ty*32+(tx&31)];
        assert((entry>>12)==(map_line_color>>4));flips_seen|=1u<<((entry>>10)&3);
        unsigned px=x&7,py=y&7;if(entry&0x400)px=7-px;if(entry&0x800)py=7-py;
        unsigned got=(v[0x8000+(entry&1023)*32+py*4+px/2]>>((px&1)*4))&15;
        int wx=(int)x-ox+512,wy=(int)y-oy+64;
        unsigned want=x>=left&&x<right&&y>=top&&y<bottom&&(unsigned)wx<1024&&(unsigned)wy<512&&oracle[wy][wx]?(map_line_color&15):0;
        if(got!=want){fprintf(stderr,"%s frame%u mode%u anchor%d,%d hero%d,%d cachepixel%u,%u world%d,%d got%u want%u dict%u\n",label,frames,mode,map_last_x,map_last_y,game.x,game.y,x,y,wx-512,wy-64,got,want,map_unique_tiles);exit(2);}
    }
    canaries();pixels_checked+=(uint64_t)width*256;frames++;mode_frames[mode]++;
}
static void camera(int x,int y){
    game.x=x;game.y=y;cam_x=(game.area?1536:DUNGEON_ORIGIN)+(x-y)*16-120;
    cam_y=(game.area?128:72)+(x+y)*8-84;
    map_ref_x=map_round((x-y)*640,180);map_ref_y=map_round((x+y)*640,360);
}
static void fog_add(unsigned x,unsigned y){
    assert(x<40&&y<40);unsigned n=y*40+x,mask=1u<<(n&7);
    if(!(fog_bits[n>>3]&mask)){fog_bits[n>>3]|=mask;revision++;branch_cells++;}
}
static void commit(void){
    assert(map_pending==2);R16(0x04000006)=160;automap_commit();assert(!map_pending&&!map_init);check_pixels();
}
static void fresh(unsigned mode,int x,int y){
    memset(vram,0xcc,sizeof vram);memset(revealed,0xa6,sizeof revealed);memset(outline,0xa6,sizeof outline);
    map_visible=1;map_init=0;map_pending=0;map_last_x=-1;map_last_area=game.area;map_last_revision=~revision;
    map_screen_fallback=mode;map_screen_live=mode;camera(x,y);automap_build_outline();
    automap_draw();assert(map_pending==2);commit();scenes++;
}
static void update(unsigned expect_handled){
    int ax=map_last_x,ay=map_last_y;
    unsigned handled=automap_incremental(revision);assert(handled==expect_handled);
    if(handled){
        assert(map_last_x==ax&&map_last_y==ay);
        if(map_pending){partial_updates++;commit();}else{no_pixel_updates++;check_pixels();}
    }else{full_fallbacks++;automap_draw();commit();}
    assert(!memcmp(MAP_OLD_FOG,fog_bits,200));
}
static void blank_geometry(void){memset(&game,0,sizeof game);memset(fog_bits,0,200);revision++;}
static void lattice_geometry(void){
    blank_geometry();for(unsigned y=4;y<76;y++)for(unsigned x=4;x<76;x++)game.map[y][x]=(x%9!=0&&y%7!=0);
}
static void decode_all(void){
    label="hardware_decode";
    /* Asymmetric masks deliberately distinguish all flips; varying nonzero
     * nibble values means decode cannot accidentally be tied to current ink. */
    for(unsigned bank=0;bank<16;bank++)for(unsigned ink=1;ink<=15;ink++)for(unsigned flip=0;flip<4;flip++){
        unsigned tile=530;uint8_t *v=(uint8_t*)vram+0x8000+tile*32;memset(v,0,32);
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)if((x*3+y*5+bank)%11<3){v[y*4+x/2]|=ink<<((x&1)*4);}
        uint32_t bits[2]={0,0};map_read_live_tile(tile|(flip<<10)|(bank<<12),bits);
        const uint8_t *b=(const uint8_t*)bits;
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
            unsigned sx=(flip&1)?7-x:x,sy=(flip&2)?7-y:y;
            assert(((b[y]>>x)&1)==((sx*3+sy*5+bank)%11<3));
        }
        explicit_decode_cases++;
    }
}
static void branch_sequence(unsigned mode){
    label="branch_sequence";lattice_geometry();fog_add(19,19);fresh(mode,40,40);
    static const unsigned cells[][2]={{20,19},{21,19},{20,20},{19,20},{18,19},{19,18},{21,20},{20,21},{18,20},{18,18},{22,19},{19,21}};
    for(unsigned i=0;i<sizeof cells/sizeof cells[0];i++){
        fog_add(cells[i][0],cells[i][1]);camera(40+(i&1),40);update(mode!=1);
        unsigned writes=dma_writes,oldcount=map_unique_tiles;automap_draw();assert(!map_pending&&writes==dma_writes&&oldcount==map_unique_tiles);repeat_checks++;
    }
    /* Reveal an off-window cell: snapshot/revision must advance without upload. */
    if(mode!=1){fog_add(0,0);unsigned writes=dma_writes;update(1);assert(dma_writes==writes);}
    label="shrink";memset(fog_bits,0,200);revision++;fog_add(20,20);update(0);
    label="reveal_all";memset(fog_bits,255,200);revision++;threshold_rejections++;update(0);
    if(!(map_screen_live&1)){
        label="recenter";camera(65,65);int ax=map_last_x,ay=map_last_y;
        assert(!automap_cache_contains_view());automap_draw();commit();assert(map_last_x!=ax||map_last_y!=ay);recenters++;
    }
}
static void cancellation_sequence(unsigned hidden,unsigned late){
    lattice_geometry();fog_add(19,19);fresh(0,40,40);label=hidden?"hidden_cancel":"late_cancel";
    fog_add(19,20);assert(automap_incremental(revision)&&map_pending==2);
    uint8_t saved[65536];memcpy(saved,vram,sizeof saved);unsigned commits=map_commits;
    map_visible=!hidden;R16(0x04000006)=late;automap_commit();
    assert(!map_pending&&map_last_x==-1&&map_last_revision==0xffffffffu&&map_commits==commits);
    assert(!memcmp(saved,vram,sizeof saved));cancellations++;
    /* Simulate a normal draw using all staging scratch before map re-enable. */
    memset(load_buffer,0x39,sizeof load_buffer);map_visible=1;automap_draw();commit();
}
static void menu_sequence(void){
    lattice_geometry();fog_add(19,19);fog_add(20,19);fresh(0,40,40);label="menu_invalidation";
    memset((uint8_t*)vram+0xc240,0x5c,1312);map_init=1;map_last_x=-1;map_pending=0;map_last_revision=0xffffffffu;
    fog_add(21,19);assert(!automap_incremental(revision));automap_draw();commit();menu_restores++;
}
static void threshold_sequence(void){
    lattice_geometry();fog_add(19,19);fresh(0,40,40);label="threshold_24";
    for(unsigned y=17;y<22;y++)for(unsigned x=17;x<22;x++)if(x!=19||y!=19)fog_add(x,y);
    update(1);
    lattice_geometry();fog_add(19,19);fresh(0,40,40);label="threshold_25";
    for(unsigned y=17;y<22;y++)for(unsigned x=17;x<22;x++)if(x!=19||y!=19)fog_add(x,y);
    fog_add(22,20);assert(!automap_incremental(revision));threshold_rejections++;automap_draw();commit();
}
static void dictionary_sequence(void){
    /* Create a valid, fully occupied live atlas using production full packing,
     * then require a new distinct mask. References alias the old tiles with
     * flips, so modifying an existing dictionary entry would fail the oracle. */
    label="dictionary_capacity";lattice_geometry();fog_add(19,19);fresh(0,40,40);
    unsigned oldcount=map_unique_tiles;uint32_t *dict=MAP_DICTIONARY_BITS;
    for(unsigned n=0;n<oldcount;n++)map_read_live_tile(map_tile(n),dict+n*2);
    for(unsigned n=oldcount;n<MAP_DICTIONARY;n++){
        /* Dense shapes cannot equal any thin minimap contour added here. */
        dict[n*2]=0xffffffffu;dict[n*2+1]=0xffff0000u|n;
    }
    map_unique_tiles=MAP_DICTIONARY;automap_prepare_tiles();
    /* Upload atlas only, leaving the live screen references untouched. */
    for(unsigned n=0;n<MAP_DICTIONARY;n++)memcpy((uint8_t*)vram+0x8000+map_tile(n)*32,MAP_UPLOAD_TILES+n*8,32);
    uint8_t saved[65536];memcpy(saved,vram,sizeof saved);fog_add(19,20);
    assert(!automap_incremental(revision));assert(!map_pending&&map_unique_tiles==MAP_DICTIONARY);
    assert(!memcmp(saved,vram,sizeof saved));capacity_rejections++;
    automap_draw();commit();assert(map_unique_tiles<MAP_DICTIONARY);full_fallbacks++;
}
static void native_sequence(unsigned level,unsigned seed){
    label="native_sequence";blank_geometry();memset(&quest_progress,0,sizeof quest_progress);dungeon_level=level;
    game_generate_dungeon(&game,seed);fog_add(20,20);fresh(0,40,40);native_scenes++;
    /* Grow a connected, branching 13x13 region, small increments. Repeated
     * atlas exhaustion must compact cleanly without accumulating stale lines. */
    for(unsigned radius=1;radius<=6;radius++){
        for(int edge=0;edge<4;edge++)for(int step=-(int)radius;step<(int)radius;step++){
            int x=20,y=20;
            if(edge==0){x+=step;y-=radius;}if(edge==1){x+=radius;y+=step;}
            if(edge==2){x-=step;y+=radius;}if(edge==3){x-=radius;y-=step;}
            fog_add((unsigned)x,(unsigned)y);
            int ax=map_last_x,ay=map_last_y;unsigned reads=dma_reads;
            automap_draw();
            if(map_pending){if(dma_reads>reads){partial_updates++;assert(map_last_x==ax&&map_last_y==ay);}else full_fallbacks++;commit();}
            else{no_pixel_updates++;check_pixels();}
        }
    }
}
int main(int argc,char **argv){
    assert(argc==2);setbuf(stdout,0);
    for(unsigned i=0;i<8;i++)scratch_guard.before[i]=scratch_guard.after[i]=0xa5a5a5a5;
    FILE *f=fopen(argv[1],"rb");assert(f);uint8_t town[9216];assert(fread(town,1,sizeof town,f)==sizeof town);fclose(f);town_pack_collision(town);
    decode_all();
    for(unsigned mode=0;mode<3;mode++)branch_sequence(mode);
    cancellation_sequence(1,160);cancellation_sequence(0,159);cancellation_sequence(0,171);
    menu_sequence();threshold_sequence();dictionary_sequence();
    static const unsigned levels[]={1,4,5,9,13,16};
    for(unsigned i=0;i<sizeof levels/sizeof levels[0];i++)native_sequence(levels[i],1);
    label="town_full_only";game.area=1;fresh(0,48,48);assert(!automap_incremental(++revision));
    assert(flips_seen==15&&mode_frames[0]&&mode_frames[1]&&mode_frames[2]);
    assert(partial_updates>50&&no_pixel_updates&&capacity_rejections&&threshold_rejections&&menu_restores&&cancellations==3);
    assert(!map_commit_overruns);
    printf("{\"status\":\"PASS\",\"scenes\":%u,\"native_scenes\":%u,\"pixel_exact_frames\":%u,\"cached_pixels_compared\":%llu,\"mode_frames\":[%u,%u,%u],\"hardware_flip_mask\":%u,\"explicit_decode_cases\":%u,\"partial_updates\":%u,\"no_pixel_updates\":%u,\"full_fallbacks\":%u,\"branch_cells\":%u,\"repeat_checks\":%u,\"recenters\":%u,\"pending_cancellations\":%u,\"menu_restores\":%u,\"dictionary_capacity_rejections\":%u,\"threshold_rejections\":%u,\"vram_read_dmas\":%u,\"vram_write_dmas\":%u}\n",scenes,native_scenes,frames,(unsigned long long)pixels_checked,mode_frames[0],mode_frames[1],mode_frames[2],flips_seen,explicit_decode_cases,partial_updates,no_pixel_updates,full_fallbacks,branch_cells,repeat_checks,recenters,cancellations,menu_restores,capacity_rejections,threshold_rejections,dma_reads,dma_writes);
    return 0;
}
