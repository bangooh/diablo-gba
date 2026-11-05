#include "game.h"
#include "world.h"
#include "worldgen/fast_seed.h"
#include "world_rooms.h"
#include "generation_context.h"
#include "coop.h"
#include "../generated/anvil_guards.h"
#include "../generated/valor_guards.h"
#include "../generated/blind_guards.h"
#include "../generated/banner_room.h"
#include "../generated/king_quest.h"
#include "../generated/water_quest.h"
#include "../generated/butcher_assets.h"
#include "../generated/environment_props.h"
/* Source-guided L5 adaptation. Original 40x40 macros / 80x80 walking cells.
 * Scratch is borrowed from inactive scene RAM; no large IWRAM stack arrays. */
unsigned dungeon_level=1,butcher_quest;
uint32_t campaign_seed=1;
int game_wounded_present(void){return butcher_quest==1||butcher_quest==2;}
static uint32_t next_seed=1;
void game_seed_next(uint32_t seed){next_seed=seed?seed:1;}
static uint32_t rnd(uint32_t *s){GEN_WORK();*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void carve(uint8_t m[40][40],int x,int y,int w,int h){for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)m[j][i]=1;}
static int empty(uint8_t m[40][40],int x,int y,int w,int h){
    if(x<0||y<0||x+w>40||y+h>40)return 0;
    for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(m[j][i])return 0;return 1;
}
/* Iterative equivalent of L5roomGen; bounded explicit worklist replaces recursion. */
static void branches(uint8_t m[40][40],uint32_t *s,int x,int y,int dir){
    uint8_t (*todo)[5]=(void*)(dungeon_workspace+4000);int n=1;
    todo[0][0]=x;todo[0][1]=y;todo[0][2]=todo[0][3]=10;todo[0][4]=dir;
    while(n){--n;x=todo[n][0];y=todo[n][1];int w=todo[n][2],h=todo[n][3];dir=todo[n][4];
        int vertical=dir?rnd(s)%4!=0:rnd(s)%4==0,a=0,b=0,cw=0,ch=0,rx=0,ry=0;
        for(int tries=0;tries<20;tries++){
            cw=(2+rnd(s)%5)&~1;ch=(2+rnd(s)%5)&~1;
            rx=vertical?x+w/2-cw/2:x-cw;ry=vertical?y-ch:y+h/2-ch/2;
            a=empty(m,rx-1,ry-1,vertical?cw+2:cw+1,vertical?ch+1:ch+2);if(a)break;
        }
        if(a)carve(m,rx,ry,cw,ch);
        int bx=vertical?rx:x+w,by=vertical?y+h:ry;
        b=empty(m,vertical?bx-1:bx,vertical?by:by-1,vertical?cw+2:cw+1,vertical?ch+1:ch+2);
        if(b)carve(m,bx,by,cw,ch);
        /* LIFO: visit the first child before its sibling, as L5roomGen does. */
        if(b&&n<250){todo[n][0]=bx;todo[n][1]=by;todo[n][2]=cw;todo[n][3]=ch;todo[n++][4]=!vertical;}
        if(a&&n<250){todo[n][0]=rx;todo[n][1]=ry;todo[n][2]=cw;todo[n][3]=ch;todo[n++][4]=!vertical;}
    }
}
static void first_rooms(uint8_t m[40][40],uint32_t *s){
    int vertical=rnd(s)&1,on[3]={(int)(rnd(s)&1),(int)(rnd(s)&1),(int)(rnd(s)&1)};
    if(on[0]+on[2]<=1)on[1]=1;
    for(int i=0;i<3;i++)if(on[i])carve(m,vertical?15:1+14*i,vertical?1+14*i:15,10,10);
    int lo=on[0]?1:18,hi=on[2]?39:22;
    if(vertical)carve(m,17,lo,6,hi-lo);else carve(m,lo,17,hi-lo,6);
    for(int i=0;i<3;i++)if(on[i])branches(m,s,vertical?15:1+14*i,vertical?1+14*i:15,!vertical);
}
static int stairs_place(uint8_t m[40][40],uint32_t *s){
    static const uint8_t stairs[4][4]={{22,66,23,22},{63,64,65,2},{13,67,68,13},{13,13,13,13}};
    /* Exact non-Hellfire L5STAIRSUP search footprint: solid, north wall, two floor rows. */
    unsigned chosen=0,count=0;
    for(int y=1;y<35;y++)for(int x=1;x<35;x++){
        int ok=1;for(int j=0;j<4;j++)for(int i=0;i<4;i++)if(m[y+j][x+i]!=(j==0?22:j==1?2:13))ok=0;
        if(ok&&rnd(s)%++count==0)chosen=y*40+x;
    }
    if(!count)return 0;int x=chosen%40,y=chosen/40;
    for(int j=0;j<4;j++)for(int i=0;i<4;i++)m[y+j][x+i]=stairs[j][i];return 1;
}
static int downstairs(uint8_t m[40][40],uint32_t *s){
    int ux=0,uy=0;for(int y=0;y<40;y++)for(int x=0;x<40;x++)if(m[y][x]==64){ux=x;uy=y;}
    unsigned count=0,chosen=0;
    for(int y=1;y<36;y++)for(int x=1;x<36;x++){
        int dx=x-ux,dy=y-uy;if(dx*dx+dy*dy<225)continue;
        int ok=1;for(int j=0;j<3;j++)for(int i=0;i<4;i++)if(m[y+j][x+i]!=13)ok=0;
        if(ok&&rnd(s)%++count==0)chosen=y*40+x;
    }
    if(!count)return 0;int x=chosen%40,y=chosen/40;
    const uint8_t ids[2][3]={{62,57,58},{61,59,60}};
    for(int j=0;j<2;j++)for(int i=0;i<3;i++)m[y+j][x+i]=ids[j][i];return 1;
}
void game_dungeon_spawn(Game *g){
    if(dungeon_level==REGION_BONE){g->x=53;g->y=23;return;}
    if(dungeon_level==REGION_VILE){g->x=19;g->y=20;return;}
    if(dungeon_level==REGION_KING){g->x=67;g->y=29;return;}
    if(dungeon_level==REGION_WATER){g->x=15;g->y=67;return;}
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++)if(g->map[y][x]==2){g->x=x+(dungeon_level<=4?2:0);g->y=y+(dungeon_level<=4?2:0);return;}
    g->x=g->y=4;
}
static uint8_t banner_open[100];
static void banner_patch(uint8_t m[40][40]){
 int ox=quest_banner_cell%80/2,oy=quest_banner_cell/80/2;unsigned stage=quest_progress.stage[7];
 for(int y=0;y<10;y++)for(int x=0;x<10;x++)if((x>=6&&y>=6)||(stage>=2&&x<=6&&y<=2)||stage>=3)m[oy+y][ox+x]=banner_open[y*10+x];
}
void game_banner_change(Game *g){
 if(!game_campaign_layout()||dungeon_level!=4||!quest_progress.state[7])return;
 uint8_t (*m)[40]=(void*)dungeon_workspace;if(!game_dungeon_read_macros(g,m))return;
 banner_patch(m);game_dungeon_commit_macros(g,m);
}
static int banner_place(uint8_t m[40][40]){
 for(int y=2;y<29;y++)for(int x=2;x<29;x++){
  int ok=1;for(int j=-1;j<9;j++)for(int i=-1;i<9;i++)if(m[y+j][x+i]!=13)ok=0;if(!ok)continue;
  quest_banner_cell=y*160+x*2;
  for(int j=0;j<10;j++)for(int i=0;i<10;i++)banner_open[j*10+i]=m[y+j][x+i];
  for(int j=0;j<8;j++)for(int i=0;i<8;i++){if(banner1[j*8+i])banner_open[j*10+i]=banner1[j*8+i];m[y+j][x+i]=banner2[j*8+i]?banner2[j*8+i]:13;}
  banner_patch(m);return 1;
 }
 return 0;
}
static int king_entrance(uint8_t m[40][40]){
 for(int y=2;y<31;y++)for(int x=2;x<31;x++){
  int ok=1;for(int j=-1;j<=7;j++)for(int i=-1;i<=7;i++)if(m[y+j][x+i]!=13)ok=0;if(!ok)continue;
  quest_king_cell=y*160+x*2;
  for(int j=0;j<7;j++)for(int i=0;i<7;i++)m[y+j][x+i]=skngdo[j*7+i]?skngdo[j*7+i]:13;
  return 1;
 }
 return 0;
}
static int water_entry(uint8_t m[40][40]){
 for(int y=2;y<32;y++)for(int x=2;x<32;x++){
  int clear=1;for(int j=-1;j<=6;j++)for(int i=-1;i<=6;i++)if(m[y+j][x+i]!=13)clear=0;if(!clear)continue;
  quest_water_cell=y*160+x*2;
  for(int j=0;j<6;j++)for(int i=0;i<6;i++)if(water_entrance[j*6+i])m[y+j][x+i]=water_entrance[j*6+i];return 1;
 }
 return 0;
}
static int connected(Game *g){
    uint8_t *seen=dungeon_workspace+6400;uint16_t *q=(void*)(dungeon_workspace+12800);unsigned head=0,tail=0,total=0;
    for(int i=0;i<6400;i++)seen[i]=0;
    game_dungeon_spawn(g);unsigned start=g->y*80+g->x;if(!g->map[g->y][g->x])return 0;q[tail++]=start;seen[start]=1;
    for(int y=0;y<80;y++)for(int x=0;x<80;x++)total+=!!g->map[y][x];
    while(head<tail){int p=q[head++],x=p%80,y=p/80;const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
        for(int i=0;i<4;i++){int nx=x+dx[i],ny=y+dy[i],c=ny*80+nx;
            if(nx>=0&&ny>=0&&nx<80&&ny<80&&!seen[c]&&g->map[ny][nx]){seen[c]=1;q[tail++]=c;}}
    }
    if(game_campaign_layout()&&((dungeon_level==4&&quest_progress.state[7])||(dungeon_level==3&&quest_progress.state[12]))){for(int y=0;y<80;y++)for(int x=0;x<80;x++)if(g->map[y][x]&&!seen[y*80+x]&&game_quest_valor_room(x,y))total--;}
    if(game_campaign_layout()&&dungeon_level==3&&quest_progress.state[12]){
        int x=quest_king_cell%80+12,y=quest_king_cell/80+7,access=0;
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)access|=seen[(y+dy)*80+x+dx];
        if(!access||!seen[g->down_y*80+g->down_x])return 0;
    }
    return tail==total&&total>1600;
}
/* Original RND6 (6x6 macros), embedded in a connected clear chamber. */
static int room_x=-1,room_y=-1;
void game_generation_capture(GameGenerationState *out){
 for(unsigned i=0;i<sizeof(*out);i++)((uint8_t*)out)[i]=0;
 out->quest_cells[0]=quest_anvil_cell;out->quest_cells[1]=quest_valor_cell;out->quest_cells[2]=quest_banner_cell;out->quest_cells[3]=quest_king_cell;out->quest_cells[4]=quest_water_cell;
 out->butcher_room[0]=room_x;out->butcher_room[1]=room_y;
 for(unsigned i=0;i<sizeof banner_open;i++)out->banner_open[i]=banner_open[i];
 game_world_generation_capture(out);game_rooms_generation_capture(out);
}
void game_generation_apply(const GameGenerationState *in){
 quest_anvil_cell=in->quest_cells[0];quest_valor_cell=in->quest_cells[1];quest_banner_cell=in->quest_cells[2];quest_king_cell=in->quest_cells[3];quest_water_cell=in->quest_cells[4];
 room_x=in->butcher_room[0];room_y=in->butcher_room[1];
 for(unsigned i=0;i<sizeof banner_open;i++)banner_open[i]=in->banner_open[i];
 game_world_generation_apply(in);game_rooms_generation_apply(in);
}
static int in_butcher_room(int x,int y){return room_x>=0&&x>=room_x*2&&y>=room_y*2&&x<room_x*2+12&&y<room_y*2+12;}
int game_wall_trap_protected(int x,int y){return in_butcher_room(x,y)||game_quest_valor_room(x,y);}
static int place_butcher_room(uint8_t m[40][40],uint32_t *seed){
 unsigned count=0,chosen=0;
 for(int y=2;y<32;y++)for(int x=2;x<32;x++){
  int ok=1;for(int j=-1;j<=6;j++)for(int i=-1;i<=6;i++)if(m[y+j][x+i]!=13)ok=0;
  if(ok&&rnd(seed)%++count==0)chosen=y*40+x;
 }
 if(!count)return 0;room_x=chosen%40;room_y=chosen/40;
 for(int j=0;j<6;j++)for(int i=0;i<6;i++)m[room_y+j][room_x+i]=butcher_room[j][i];
 return 1;
}
static void populate_butcher_room(Game *g){
 int x=room_x*2,y=room_y*2,slot=0;
 for(unsigned i=0;i<sizeof(butcher_doors)/sizeof(butcher_doors[0]);i++)g->props[slot++]=(Prop){x+butcher_doors[i][0],y+butcher_doors[i][1],butcher_doors[i][2],0};
 x+=BUTCHER_ANCHOR_X;y+=BUTCHER_ANCHOR_Y;
 /* AddTortures offsets: five hanging victims, four men, three women. */
 static const int8_t decoration[12][2]={{0,1},{0,3},{2,-1},{4,-1},{0,5},{1,3},{4,5},{2,0},{3,2},{2,4},{2,1},{4,2}};
 for(int i=0;i<12;i++)g->props[slot++]=(Prop){x+decoration[i][0],y+decoration[i][1],PROP_DECOR_FIRST+i,1};
 /* PlaceUniqueMonst scans piece367, then increments both coordinates.
  * Initialize both HP fields through the shared difficulty/player-mode rules. */
 unsigned hp=game_monster_hp(MONSTER_BUTCHER,campaign_seed);
 g->enemies[MAX_ENEMIES-1]=(Enemy){x+1,y+1,hp,2,MONSTER_BUTCHER,hp};
}
#include "theme_rooms.inc"
#include "environment_remaining_generation.inc"
static int clear_prop(const Game *g,int x,int y,int big){
    if(theme_reserved(x,y))return 0;
    if(game_quest_valor_room(x,y))return 0;
    if(dungeon_level==10&&quest_anvil_cell){int dx=x-quest_anvil_cell%80,dy=y-quest_anvil_cell/80;if(dx>=-12&&dx<=12&&dy>=-12&&dy<=12)return 0;}
    if(x<2||y<3||x>77||y>77||in_butcher_room(x,y))return 0;
    for(int j=big==1?-2:-1;j<=1;j++)for(int i=-1;i<=1;i++)if(!g->map[y+j][x+i]||theme_reserved(x+i,y+j))return 0;
    for(int i=0;i<MAX_PROPS;i++)if(g->props[i].kind){int dx=g->props[i].x-x,dy=g->props[i].y-y;if(big==2&&game_prop_is_barrel(g->props[i].kind)){if(!dx&&!dy)return 0;}else if(dx>=-2&&dx<=2&&dy>=-2&&dy<=2)return 0;}
    int dx=x-g->down_x,dy=y-g->down_y;if(dx>=-5&&dx<=5&&dy>=-5&&dy<=5)return 0;
    dx=x-g->x;dy=y-g->y;return dx>5||dx<-5||dy>5||dy<-5;
}
/* L5H/VWallOk + L5Horiz/VertWall: anchored straight partitions with one
 * native door macro25/26. Bound four of each orientation to the64-prop budget.
 * The original curved/arch wall families remain in the terrain atlas; these
 * ordinary doors never invent a freestanding sprite or carve a collision hole. */
static void cathedral_partitions(uint8_t m[40][40],uint32_t *s){
 for(int vertical=0;vertical<2;vertical++){
  unsigned placed=0;
  for(int y=2;y<38&&placed<4;y++)for(int x=2;x<38&&placed<4;x++){
   unsigned wall=vertical?1:2,c=m[y][x],anchor=wall;
   if(c!=wall&&c!=3&&c!=(vertical?7u:6u))continue;
   if(c==(vertical?7u:6u))anchor=4;
   if(rnd(s)%4)continue;
   int len=1;
   while(x+(vertical?0:len)<38&&y+(vertical?len:0)<38){
    int xx=x+(vertical?0:len),yy=y+(vertical?len:0);
    if(m[yy][xx]!=13||m[yy-(vertical?0:1)][xx-(vertical?1:0)]!=13||m[yy+(vertical?0:1)][xx+(vertical?1:0)]!=13)break;
    len++;
   }
   int ex=x+(vertical?0:len),ey=y+(vertical?len:0);if(ex>=38||ey>=38||len<=2)continue;
   unsigned end=m[ey][ex];if(!((end>=3&&end<=7)||(end>=16&&end<=24&&end!=22)))continue;
   unsigned door=1+rnd(s)%(unsigned)(len-1);m[y][x]=(uint8_t)anchor;
   for(int n=1;n<len;n++)m[y+(vertical?n:0)][x+(vertical?0:n)]=(uint8_t)(n==(int)door?(vertical?25:26):wall);
   placed++;
  }
 }
}
/* Retail LAMPS miniset129,0 /130,128. Extra untouched-floor clearance
 * prevents a solid candlestick from closing any one-cell quest passage. */
static void cathedral_lamps(uint8_t m[40][40],uint32_t *s){
 unsigned placed=0,start=rnd(s)%1600;
 for(unsigned n=0;n<1600&&placed<4;n++){
  unsigned c=(start+n)%1600;int x=c%40,y=c/40;if(x<2||y<2||x>35||y>35)continue;
  int clear=1;for(int j=-1;j<=2;j++)for(int i=-1;i<=2;i++)if(m[y+j][x+i]!=13)clear=0;
  if(!clear)continue;
  m[y][x]=129;m[y+1][x]=130;m[y+1][x+1]=128;placed++;
 }
}
static unsigned cathedral_population(Game *g,uint32_t *s){
 unsigned slot=room_x>=0?13:0;
 uint8_t (*m)[40]=(void*)dungeon_workspace;if(!game_dungeon_read_macros(g,m))return slot;
 /* AddL1Objs source piece270=light,44/51/214=leftdoor,46/56=rightdoor.
  * Only newly generated ordinary structures are added here. Reserved quest
  * structures retain their existing population and thirteen Butcher slots. */
 for(int y=0;y<40;y++)for(int x=0;x<40;x++)for(unsigned r=0;r<sizeof cathedral_environment_macros/sizeof cathedral_environment_macros[0];r++){
  const uint16_t *record=cathedral_environment_macros[r];if(m[y][x]!=record[0])continue;
  for(unsigned q=0;q<4;q++){
   unsigned native=record[q+1],kind=native==270?PROP_BRAZIER:native==44||native==51||native==214?PROP_DOOR_L:native==46||native==56?PROP_DOOR_R:0;
   int xx=x*2+(q&1),yy=y*2+(q>>1);if(kind&&!in_butcher_room(xx,yy)&&slot<MAX_PROPS-5)g->props[slot++]=(Prop){xx,yy,kind,0};
  }
 }
 (void)s;
 return slot;
}
/* Devilution GetLevelMTypes / monsterdata, retail non-Hellfire.
 * Supported families and variants are imported from monstdat.cpp.
 * Golem consumes 386 of the original 4000 image budget before this call.
 * Availability mask=3; excludes unsupported ranged and special-AI families. */
unsigned game_monster_roster(unsigned level,unsigned budget,uint32_t *seed,uint8_t out[MONSTER_TYPES]){
    /* GetLevelMTypes fixes floor16 to Blood Knights, Advocates and Diablo.
     * Both ordinary members have independent native palette families. */
    if(level==16){out[0]=MONSTER_BLOOD_KNIGHT;out[1]=MONSTER_ADVOCATE;const uint8_t bases[3]={22,45,102};unsigned u=game_unique_for_roster(16,bases,3),base=game_monster_base_kind(u);if(u&&base!=MONSTER_BLOOD_KNIGHT){out[2]=base;return 3;}return 2;}
    uint8_t candidates[MONSTER_TYPES];unsigned n=0,count=0;
    int blind=level==7&&game_campaign_layout()&&quest_progress.state[8];
    if(blind){out[count++]=MONSTER_ILLUSION_WEAVER;unsigned cost=monster_defs[MONSTER_ILLUSION_WEAVER].image_cost;budget=budget>cost?budget-cost:0;}
    int banner=level==4&&game_campaign_layout()&&quest_progress.state[7];
    if(banner){out[count++]=MONSTER_BLUE_SPEAR;out[count++]=MONSTER_BLUE_SWORD;out[count++]=MONSTER_OVERLORD;budget=0;}
    int valor=level==5&&game_campaign_layout()&&quest_progress.state[9];
    if(valor){out[count++]=MONSTER_HORNED_DEMON;unsigned cost=monster_defs[MONSTER_HORNED_DEMON].image_cost;budget=budget>cost?budget-cost:0;}
    int guards=level==10&&game_campaign_layout()&&quest_progress.state[10];
    if(guards){out[count++]=MONSTER_FIRE_CLAN_ARCHER;out[count++]=MONSTER_OBSIDIAN_LORD;unsigned cost=monster_defs[MONSTER_FIRE_CLAN_ARCHER].image_cost+monster_defs[MONSTER_OBSIDIAN_LORD].image_cost;budget=budget>cost?budget-cost:0;}
    for(unsigned i=0;i<MONSTER_TYPES;i++){
        const MonsterDef *d=&monster_defs[i];
        if(!(game_campaign_layout()&&((level==4&&quest_progress.state[2]&&d->family==monster_defs[MONSTER_GHARBAD].family)||(level==8&&quest_progress.state[3]&&d->family==monster_defs[MONSTER_ZHAR].family)||(level==13&&quest_progress.state[11]&&(d->family==monster_defs[MONSTER_WARLORD].family||(d->family==5&&i!=MONSTER_BLACK_KNIGHT)))))&&!(blind&&d->family==2)&&!(valor&&d->family==monster_defs[MONSTER_HORNED_DEMON].family)&&!(guards&&(d->family==2||d->family==4))&&monster71_normal[i]&&i!=MONSTER_BUTCHER&&i!=MONSTER_SNOTSPIL&&((level>=d->min_floor&&level<=d->max_floor)||game_unique_base_on_floor(i,level))&&!(level==2&&butcher_quest&&d->family==5))candidates[n++]=i;
    }
    while(n&&count<6){
        for(unsigned i=0;i<n;)if(monster_defs[candidates[i]].image_cost>budget)candidates[i]=candidates[--n];else ++i;
        if(!n)break;
        unsigned pick=rnd(seed)%n,kind=candidates[pick],family=monster_defs[kind].family;
        out[count++]=kind;budget-=monster_defs[kind].image_cost;
        /* Hardware adaptation: one translation per shared sprite-palette family.
         * Corpses use BG colors; containers select this same family's variant. */
        for(unsigned i=0;i<n;)if(monster_defs[candidates[i]].family==family)candidates[i]=candidates[--n];else ++i;
    }
    /* A translated unique needs one BG/OBJ bank pair in addition to its base
     * followers. Reserve family3 only when PlaceUniques has an eligible leader;
     * these four floors have no fixed quest actors using that family. */
    unsigned unique=game_unique_for_roster(level,out,count);
    if(unique)for(unsigned i=0;i<count;){
        if(monster_defs[out[i]].family==game_unique_palette_family(unique))out[i]=out[--count];else i++;
    }
    return count;
}
/* PlaceUniqueMonst / PlaceGroup: native6x6 density test, eight followers and
 * ten placement retries. A unique is selected by its source base in the floor
 * roster, never by a random extra chance or a nearby ordinary monster.
 * Generation can target an uncommitted FloorState: only write g records here.
 * AI clocks/pack links are restored after the caller commits the new floor. */
static int unique_space(const Game *g,int x,int y){
 if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H||!game_walkable(g,x,y)||in_butcher_room(x,y)||game_quest_valor_room(x,y)||theme_reserved(x,y))return 0;
 int dx=x-g->x,dy=y-g->y;if(dx>=-16&&dx<=16&&dy>=-16&&dy<=16)return 0;
 dx=x-g->down_x;dy=y-g->down_y;if(dx>=-16&&dx<=16&&dy>=-16&&dy<=16)return 0;
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive&&g->enemies[i].x==x&&g->enemies[i].y==y)return 0;
 for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].x==x&&g->drops[i].y==y)return 0;
 return 1;
}
static unsigned unique_population(Game *g,uint32_t *s,unsigned kind,unsigned first,unsigned capacity){
 if(!kind||!capacity)return 0;
 static const int8_t ux[8]={0,-1,-1,-1,0,1,1,1},uy[8]={1,1,0,-1,-1,-1,0,1};
 int lx=-1,ly=0;
 for(unsigned tries=0;tries<1000;tries++){
  int x=rnd(s)%MAP_W,y=rnd(s)%MAP_H;if(!unique_space(g,x,y))continue;
  unsigned free=0;for(int dy=-3;dy<3;dy++)for(int dx=-3;dx<3;dx++)free+=unique_space(g,x+dx,y+dy);
  if(free>=9){lx=x;ly=y;break;}
 }
 /* Bounded native fallback after1000 density failures. Never loop forever on
  * a fully occupied or quest-reserved map. */
 if(lx<0){unsigned start=rnd(s)%(MAP_W*MAP_H);for(unsigned n=0;n<MAP_W*MAP_H;n++){
  unsigned c=(start+n)%(MAP_W*MAP_H);if(unique_space(g,c%MAP_W,c/MAP_W)){lx=c%MAP_W;ly=c/MAP_W;break;}
 }}
 if(lx<0)return 0;
 unsigned hp=game_monster_hp(kind,rnd(s));g->enemies[first]=(Enemy){lx,ly,hp,1,kind,hp};g->enemy_facing[first]=rnd(s)%8;
 unsigned target=monster71_unique_pack[kind]?(capacity>9?8:capacity-1):0,placed=0,member=game_unique_minion_kind(kind);
 for(unsigned attempt=0;attempt<10&&target;attempt++){
  while(placed)g->enemies[first+placed--]=(Enemy){0};
  unsigned dir=rnd(s)%8;int ax=lx+ux[dir],ay=ly+uy[dir],x=ax,y=ay;
  int room=game_room_candidate_at(ax,ay);
  unsigned failed=0;
  while(placed<target&&failed<100){
   unsigned cx=(unsigned)(x-ax+3),cy=(unsigned)(y-ay+3);
   if(cx<7&&cy<7&&room>=0&&game_room_candidate_at(x,y)==room&&unique_space(g,x,y)){
    unsigned slot=first+1+placed++;hp=game_monster_hp(member,rnd(s));
    g->enemies[slot]=(Enemy){x,y,hp,1,member,hp};g->enemy_facing[slot]=rnd(s)%8;
   }else failed++;
   /* Retail uses independently rolled axes; correct its documented offset_x
    * typo for Y while preserving the source random-walk group procedure. */
   x+=ux[rnd(s)%8];y+=uy[rnd(s)%8];
  }
  if(placed==target)break;
 }
 return placed+1;
}
static void population(Game *g,uint32_t *s,uint32_t world_seed){
    uint8_t roster[MONSTER_TYPES];unsigned types=game_monster_roster(dungeon_level,4000-386-(dungeon_level==2&&butcher_quest?980:0),s,roster);
    if(!types){roster[0]=0;types=1;} /* defensive only: supported floors have candidates */
    unsigned unique=game_unique_for_roster(dungeon_level,roster,types);
    for(int i=0;i<MAX_DROPS;i++)g->drops[i].active=0;
    for(int i=0;i<MAX_PROPS;i++)g->props[i]=(Prop){0};
    for(unsigned i=0;i<MAX_ENEMIES;i++){g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;}
    unsigned door_slots=dungeon_level<=4?cathedral_population(g,s):game_world_doors_prepared(g);
    /* Cathedral: sarcophagi10..14; three chest sizes5..9,3..5,1..4;
     * barrels3..7 clusters. Clearance keeps every corridor traversable. */
    int counts[4]={10+(int)(rnd(s)%5),5+(int)(rnd(s)%5),3+(int)(rnd(s)%3),1+(int)(rnd(s)%4)};
    if(dungeon_level>4)counts[0]=0;
    const uint8_t kinds[4]={PROP_SARC,PROP_CHEST,PROP_CHEST_MEDIUM,PROP_CHEST_LARGE};
    int groups=3+rnd(s)%5,slot=(int)door_slots;
    unsigned prop_limit=dungeon_level==13&&game_campaign_layout()&&quest_progress.state[11]?54:game_campaign_layout()&&dungeon_level==2&&quest_progress.state[13]?MAX_PROPS-5:62;
    slot=(int)theme_population(g,s,roster,types,prop_limit-remaining_reserve());
    slot=(int)remaining_population(g,prop_limit,world_seed);
    for(int type=0;type<4;type++)for(int n=0;n<counts[type]&&slot<(int)prop_limit;n++)for(int tries=0;tries<500;tries++){int kind=kinds[type];
        int x=2+rnd(s)%76,y=3+rnd(s)%74;if(clear_prop(g,x,y,kind==1)){g->props[slot++]=(Prop){x,y,kind,0};break;}}
    /* Native AddBarrels:3..7 groups,1/4 explosive heads,1/5 followers,
     * and up to three adjacent random-walk attempts for each next barrel. */
    static const int8_t bxadd[8]={-1,0,1,-1,1,-1,0,1},byadd[8]={-1,-1,-1,0,0,1,1,1};
    for(int group=0;group<groups&&slot<(int)prop_limit;group++){
        int bx=2+rnd(s)%76,by=3+rnd(s)%74,tries=0;
        while(tries++<500&&!clear_prop(g,bx,by,2)){bx=2+rnd(s)%76;by=3+rnd(s)%74;}
        if(!clear_prop(g,bx,by,2))continue;
        g->props[slot++]=(Prop){bx,by,rnd(s)%4?PROP_BARREL:PROP_EXPLOSIVE_BARREL,0};
        unsigned count=1,chance=0;
        while(slot<(int)prop_limit&&(!chance||rnd(s)%chance==0)){
            int found=0;for(int step=0;step<3;step++){unsigned dir=rnd(s)%8;bx+=bxadd[dir];by+=byadd[dir];if(clear_prop(g,bx,by,2)){found=1;break;}}
            if(!found)break;
            g->props[slot++]=(Prop){bx,by,rnd(s)%5?PROP_BARREL:PROP_EXPLOSIVE_BARREL,0};chance=++count>>1;
        }
    }
    unsigned fixed=0;
    game_quest_spawn(g);
    if(dungeon_level==10&&game_campaign_layout()&&quest_progress.state[10]&&quest_anvil_cell){
      for(unsigned i=0;i<17;i++){int x=quest_anvil_cell%80+anvil_guards[i][0],y=quest_anvil_cell/80+anvil_guards[i][1];unsigned kind=anvil_guards[i][2];
       g->enemies[fixed++]=(Enemy){x,y,game_monster_hp(kind,rnd(s)),1,kind,0};}
    }
    if(dungeon_level==4&&game_campaign_layout()&&quest_progress.state[7]){
      for(unsigned i=0;i<sizeof(banner_guards)/sizeof(banner_guards[0]);i++){const uint8_t *p=banner_guards[i];g->enemies[fixed++]=(Enemy){quest_banner_cell%80+p[0],quest_banner_cell/80+p[1],game_monster_hp(p[2],rnd(s)),1,p[2],0};}
    }
    if(dungeon_level==5&&game_campaign_layout()&&quest_progress.state[9]){for(unsigned i=0;i<12;i++){int x=quest_valor_cell%80+valor_guards[i][0],y=quest_valor_cell/80+valor_guards[i][1];g->enemies[fixed++]=(Enemy){x,y,game_monster_hp(MONSTER_HORNED_DEMON,rnd(s)),1,MONSTER_HORNED_DEMON,0};}}
    if(dungeon_level==7&&game_campaign_layout()&&quest_progress.state[8])for(unsigned i=0;i<18;i++){int x=quest_valor_cell%80+blind_guards[i][0],y=quest_valor_cell/80+blind_guards[i][1];g->enemies[fixed++]=(Enemy){x,y,game_monster_hp(MONSTER_ILLUSION_WEAVER,rnd(s)),1,MONSTER_ILLUSION_WEAVER,0};}
    unsigned reserve=((dungeon_level==4||dungeon_level==8)&&game_campaign_layout()?2:0)+game_prop_reservations(g)+(room_x>=0?1:0)+(dungeon_level==4&&game_campaign_layout()&&quest_progress.state[7]?1:0);
    unsigned capacity=MAX_ENEMIES-fixed-reserve;
    fixed+=unique_population(g,s,unique,fixed,capacity);
    capacity=MAX_ENEMIES-fixed-reserve;
    fixed+=theme_monsters(g,s,roster,types,fixed,capacity);
    uint16_t *cells=(void*)(dungeon_workspace+12800);unsigned count=0,floors=0;
    for(int y=0;y<80;y++)for(int x=0;x<80;x++)if(g->map[y][x]){
        floors++;int dx=x-g->x,dy=y-g->y,ok=dx>16||dx<-16||dy>16||dy<-16;
        dx=x-g->down_x;dy=y-g->down_y;if(dx>=-16&&dx<=16&&dy>=-16&&dy<=16)ok=0;
        for(int i=0;i<64;i++)if(g->props[i].kind&&g->props[i].x==x&&g->props[i].y==y)ok=0;
        for(unsigned i=0;i<fixed;i++)if(g->enemies[i].x==x&&g->enemies[i].y==y)ok=0;
        if(ok&&!theme_reserved(x,y)&&!game_quest_valor_room(x,y)&&!in_butcher_room(x,y)&&game_walkable(g,x,y))cells[count++]=y*80+x;
    }
    for(unsigned i=count;i>1;i--){unsigned j=rnd(s)%i;uint16_t t=cells[i-1];cells[i-1]=cells[j];cells[j]=t;}
    unsigned wanted=floors/30;capacity=MAX_ENEMIES-fixed-reserve;if(wanted>capacity)wanted=capacity;if(wanted>count)wanted=count;
    /* Original InitMonsters uses single scattered monsters on floor1, not packs. */
    for(unsigned i=fixed;i<MAX_ENEMIES;i++){
        if(i<fixed+wanted){unsigned c=cells[i-fixed];unsigned kind=roster[rnd(s)%types];g->enemies[i]=(Enemy){c%80,c/80,game_monster_hp(kind,rnd(s)),1,kind,0};}
        else g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;
    }
    game_quest_npc_spawn(g);theme_zhar(g);
    if(dungeon_level==13&&game_campaign_layout()&&quest_progress.state[11])game_late_population(g);
}
void game_generate_dungeon(Game *g,uint32_t seed){
    if(dungeon_level>4){uint32_t s=seed;room_x=room_y=-1;game_world_generate(g,seed);g->kills=0;if(dungeon_level==REGION_BONE)game_late_population(g);else if(dungeon_level==REGION_KING)game_king_population(g);else if(dungeon_level==REGION_WATER)game_water_population(g);else if(dungeon_level==REGION_VILE||dungeon_level==16)game_finale_population(g);else population(g,&s,seed);if(dungeon_level==REGION_BONE||dungeon_level==REGION_WATER)game_world_doors(g,seed);game_wall_traps_generate(g,seed);return;}
    uint32_t s=seed?seed:1;uint8_t (*m)[40]=(void*)dungeon_workspace,(*plan)[40]=(void*)(dungeon_workspace+1600);
    static const uint8_t conv[16]={22,13,1,13,2,13,13,13,4,13,1,13,2,13,16,13};
    room_x=room_y=-1;int quest_room=dungeon_level==2&&butcher_quest,banner=dungeon_level==4&&game_campaign_layout()&&quest_progress.state[7],king=dungeon_level==3&&game_campaign_layout()&&quest_progress.state[12],water=dungeon_level==2&&game_campaign_layout()&&quest_progress.state[13];
    /* Retail DRLG_L5 retries until the floor-specific minimum area is met.
     * An attempt cap must not replace Cathedral rooms with a rectangular arena. */
    unsigned attempts=0;
    for(;;){
        world_seed_candidate(&s,dungeon_level,dungeon_level==2?(!!quest_room+2*!!water):dungeon_level==3?!!king:dungeon_level==4?!!banner:0,seed,&attempts);
        room_x=room_y=-1;
        for(int y=0;y<40;y++)for(int x=0;x<40;x++)m[y][x]=0;
        first_rooms(m,&s);if(banner||king||water)carve(m,13,13,14,14);int area=0;for(int y=0;y<40;y++)for(int x=0;x<40;x++)area+=m[y][x];if(area<(dungeon_level==1?533:dungeon_level==2?693:761))continue;
        for(int y=0;y<40;y++)for(int x=0;x<40;x++)plan[y][x]=22;
        for(int y=0;y<39;y++)for(int x=0;x<39;x++)plan[y][x]=conv[m[y][x]+2*m[y][x+1]+4*m[y+1][x]+8*m[y+1][x+1]];
        game_dungeon_fix_walls(plan,0);
        cathedral_partitions(plan,&s);
        if(banner&&!banner_place(plan))continue;
        if(king&&!king_entrance(plan))continue;
        if(water&&!water_entry(plan))continue;
        if(!stairs_place(plan,&s)||(!banner&&!downstairs(plan,&s)))continue;
        if(quest_room&&!place_butcher_room(plan,&s))continue;
        /* L5 chamber pillars: a four-column square, only inside an untouched
         * 9x9 floor interior. Stairs and arcade passages therefore stay clear. */
        int chamber=0;
        for(int y=1;y<31&&!chamber;y++)for(int x=1;x<31&&!chamber;x++){
            int clear=1;for(int j=0;j<9;j++)for(int i=0;i<9;i++)if(plan[y+j][x+i]!=13)clear=0;
            if(clear){plan[y+3][x+3]=plan[y+3][x+6]=plan[y+6][x+3]=plan[y+6][x+6]=15;chamber=1;}
        }
        cathedral_lamps(plan,&s);
        if(game_dungeon_commit_macros(g,plan)&&connected(g))break;
    }

    game_rooms_prepare_l1(g);game_dungeon_spawn(g);g->kills=0;population(g,&s,seed);if(room_x>=0)populate_butcher_room(g);game_wall_traps_generate(g,seed);
}
uint32_t game_floor_seed(uint32_t campaign,unsigned level){
 uint32_t s=campaign ^ (level*0x9e3779b9u);s^=s>>16;s*=0x85ebca6bu;s^=s>>13;s*=0xc2b2ae35u;s^=s>>16;return s?s:1;
}
void game_generate_next(Game *g){game_cave_policy=2;uint32_t seed=next_seed;rnd(&next_seed);campaign_seed=seed;game_quest_init(&quest_progress,seed);dungeon_level=1;butcher_quest=item_seed(seed^0x51524731u)%3!=0;game_generate_dungeon(g,game_floor_seed(seed,1));}
