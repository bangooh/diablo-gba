/* Modified GBA adaptation of Devilution themes.cpp (commit9f017572).
 * Original Sustainable Use License is retained in LICENSES/Devilution-LICENSE.md (repository root).
 * Native room/footprint/wall/type rules, with bounded atomic furniture placement
 * and an extra connectivity check for the64-object GBA limit. No live RAM. */
#include "theme_rooms.h"
#include "../generated/theme_rules.h"
enum { THEME_BARREL,THEME_SHRINE,THEME_MONSTPIT,THEME_SKELROOM,THEME_TREASURE,
 THEME_LIBRARY,THEME_TORTURE,THEME_BLOODFOUNTAIN,THEME_DECAPITATED,
 THEME_PURIFYINGFOUNTAIN,THEME_ARMORSTAND,THEME_GOATSHRINE,THEME_CAULDRON,
 THEME_MURKYFOUNTAIN,THEME_TEARFOUNTAIN,THEME_BRNCROSS,THEME_WEAPONRACK };
typedef struct {uint8_t room,type,x,y,orientation,monster;} ThemeRecord;
typedef struct {
 uint8_t held[256],excluded[256],before[800],seen[800],blocked[800],slots[64];
 uint16_t area[256],queue[6400];
 ThemeRecord rooms[WORLD_THEME_LIMIT];Prop proposed[MAX_PROPS];
 uint8_t count,proposed_count,overflow,armor,weapon,zhar,zhar_x,zhar_y;
 uint32_t unique;
} ThemeScratch;
enum { THEME_SCRATCH_OFFSET=98000 };
#define theme_scratch ((ThemeScratch*)(dungeon_workspace+THEME_SCRATCH_OFFSET))
_Static_assert((unsigned)THEME_SCRATCH_OFFSET>=(unsigned)GAME_ROOMS_SCRATCH_END,"theme scratch overlaps transmission flood");
_Static_assert(THEME_SCRATCH_OFFSET+sizeof(ThemeScratch)<=120000,"theme scratch overlaps prop lookup");
static unsigned theme_random(uint32_t *seed,unsigned limit){return limit?rnd(seed)%limit:0;}
static unsigned theme_native_flags(int x,int y){
 if((unsigned)x>=80||(unsigned)y>=80)return 1;
 unsigned macro=dungeon_workspace[(y/2)*40+x/2],flags=85;
 if(dungeon_level<=4){if(macro<sizeof theme_macro_flags_1)flags=theme_macro_flags_1[macro];}
 else if(dungeon_level<=8){if(macro<sizeof theme_macro_flags_2)flags=theme_macro_flags_2[macro];}
 else if(dungeon_level<=12){if(macro<sizeof theme_macro_flags_3)flags=theme_macro_flags_3[macro];}
 else if(macro<sizeof theme_macro_flags_4)flags=theme_macro_flags_4[macro];
 return (flags>>(((y&1)*2+(x&1))*2))&3;
}
static int theme_room(int x,int y){return game_room_candidate_at(x,y);}
static int theme_reserved(int x,int y){int id=theme_room(x,y);return id>=0&&theme_scratch->held[id];}
static int theme_prop_at(const Prop *p,int x,int y){return p->kind&&((p->x==x&&p->y==y)||game_prop_blocks(p,x,y));}
static int theme_occupied(const Game *g,int x,int y){
 if(theme_scratch->zhar&&theme_scratch->zhar_x==x&&theme_scratch->zhar_y==y)return 1;
 for(unsigned i=0;i<MAX_PROPS;i++)if(theme_prop_at(&g->props[i],x,y))return 1;
 for(unsigned i=0;i<theme_scratch->proposed_count;i++)if(theme_prop_at(&theme_scratch->proposed[i],x,y))return 1;
 return 0;
}
static int theme_floor(const Game *g,int x,int y){
 if((unsigned)x>=80||(unsigned)y>=80||!g->map[y][x])return 0;
 unsigned c=(unsigned)y*80+x;return !(theme_scratch->blocked[c>>3]&(1u<<(c&7)));
}
static void theme_reachable(const Game *g,uint8_t *seen){
 ThemeScratch *t=theme_scratch;for(unsigned i=0;i<800;i++)seen[i]=t->blocked[i]=0;
 /* Build this generation-only collision mask once per flood, rather than
  * rescanning64 props for each of four edges of every visited tile. */
 for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&g->props[i];if(!p->kind||game_prop_is_door(p->kind))continue;
  for(int y=(int)p->y-1;y<=p->y;y++)for(int x=(int)p->x-1;x<=p->x;x++)if((unsigned)x<80&&(unsigned)y<80&&game_prop_blocks(p,x,y)){
   unsigned c=(unsigned)y*80+x;t->blocked[c>>3]|=(uint8_t)(1u<<(c&7));
  }
 }
 if(!theme_floor(g,g->x,g->y))return;
 unsigned head=0,tail=1;t->queue[0]=(uint16_t)(g->y*80+g->x);seen[t->queue[0]>>3]|=(uint8_t)(1u<<(t->queue[0]&7));
 static const int8_t dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
 while(head<tail){unsigned c=t->queue[head++];for(unsigned d=0;d<4;d++){
  int x=(int)(c%80)+dx[d],y=(int)(c/80)+dy[d];if(!theme_floor(g,x,y))continue;
  unsigned n=(unsigned)y*80+x;if(seen[n>>3]&(1u<<(n&7)))continue;
  seen[n>>3]|=(uint8_t)(1u<<(n&7));t->queue[tail++]=(uint16_t)n;
 }}
}
static int theme_obj3(const Game *g,int x,int y,int id){
 for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++)if((theme_native_flags(x+i,y+j)&1)||theme_room(x+i,y+j)!=id||theme_occupied(g,x+i,y+j))return 0;
 return 1;
}
static int theme_fit5(int id,int *px,int *py,uint32_t *seed){
 (void)theme_random(seed,5); /* Native's repeated successful scan selects the first fit. */
 for(int y=2;y<78;y++)for(int x=2;x<78;x++){
  int ok=1;for(int j=-2;j<=2&&ok;j++)for(int i=-2;i<=2;i++)if((theme_native_flags(x+i,y+j)&1)||theme_room(x+i,y+j)!=id){ok=0;break;}
  if(ok){*px=x;*py=y;return 1;}
 }
 return 0;
}
static int theme_fit3(const Game *g,int id,int *px,int *py,uint32_t *seed){
 static const uint8_t frequency[4]={4,4,3,5};
 for(int y=1;y<79;y++)for(int x=1;x<79;x++)if(theme_obj3(g,x,y,id)){
  int ok=1;for(unsigned n=0;n<9;n++)if(!theme_random(seed,frequency[(dungeon_level-1)/4])){ok=0;break;}
  if(ok){*px=x;*py=y;return 1;}
 }
 return 0;
}
static int theme_fit_wall(const Game *g,int id,int *px,int *py,int *orientation){
 for(int y=1;y<79;y++)for(int x=1;x<79;x++)if(theme_room(x,y)==id){
  if((theme_native_flags(x,y-1)&2)&&!(theme_native_flags(x-1,y)&1)&&!(theme_native_flags(x+1,y)&1)&&
     theme_room(x-1,y)==id&&theme_room(x+1,y)==id&&!theme_occupied(g,x-1,y-1)&&!theme_occupied(g,x+1,y-1)){
   *px=x;*py=y;*orientation=1;return 1;
  }
  if((theme_native_flags(x-1,y)&2)&&!(theme_native_flags(x,y-1)&1)&&!(theme_native_flags(x,y+1)&1)&&
     theme_room(x,y-1)==id&&theme_room(x,y+1)==id&&!theme_occupied(g,x-1,y-1)&&!theme_occupied(g,x-1,y+1)){
   *px=x;*py=y;*orientation=0;return 1;
  }
 }
 return 0;
}
static void theme_add(const Game *g,int x,int y,unsigned kind,unsigned state){
 ThemeScratch *t=theme_scratch;
 if(t->proposed_count>=MAX_PROPS||(unsigned)x>=80||(unsigned)y>=80||theme_occupied(g,x,y)){t->overflow=1;return;}
 Prop p={(uint8_t)x,(uint8_t)y,(uint8_t)kind,(uint8_t)state};
 for(int yy=y-1;yy<=y;yy++)for(int xx=x-1;xx<=x;xx++)if(game_prop_blocks(&p,xx,yy)&&((theme_native_flags(xx,yy)&1)||theme_occupied(g,xx,yy))){t->overflow=1;return;}
 t->proposed[t->proposed_count++]=p;
}
static int theme_fit(const Game *g,unsigned type,int id,const uint8_t *roster,unsigned types,uint32_t *seed,ThemeRecord *out){
 ThemeScratch *t=theme_scratch;int x=0,y=0,orientation=0;unsigned monster=255;
 if((type==THEME_SHRINE||type==THEME_LIBRARY||type==THEME_SKELROOM)&&dungeon_level>8)return 0;
 if((type==THEME_ARMORSTAND||type==THEME_WEAPONRACK)&&dungeon_level<=4)return 0;
 if(type==THEME_CAULDRON&&dungeon_level<=12)return 0;
 unsigned once=(type==THEME_TREASURE||type==THEME_BLOODFOUNTAIN||type==THEME_PURIFYINGFOUNTAIN||type==THEME_CAULDRON||type==THEME_MURKYFOUNTAIN||type==THEME_TEARFOUNTAIN);
 if(once&&(t->unique&(1u<<type)))return 0;
 if(type==THEME_SKELROOM||type==THEME_GOATSHRINE){
  for(unsigned i=0;i<types;i++){unsigned k=roster[i];if(type==THEME_SKELROOM?(k==1||k==5||(k>=9&&k<=14)||(k>=34&&k<=36)):(k>=15&&k<=18)||k==27||k==MONSTER_FLESH_BOW){monster=k;break;}}
  if(monster==255)return 0;
 }
 int fit=1;
 if(type==THEME_SHRINE||type==THEME_LIBRARY)fit=theme_fit_wall(g,id,&x,&y,&orientation);
 else if(type==THEME_SKELROOM||type==THEME_GOATSHRINE||type==THEME_BLOODFOUNTAIN||type==THEME_PURIFYINGFOUNTAIN||type==THEME_CAULDRON||type==THEME_MURKYFOUNTAIN||type==THEME_TEARFOUNTAIN)fit=theme_fit5(id,&x,&y,seed);
 else if(type==THEME_TORTURE||type==THEME_DECAPITATED||type==THEME_ARMORSTAND||type==THEME_BRNCROSS||type==THEME_WEAPONRACK)fit=theme_fit3(g,id,&x,&y,seed);
 if(!fit)return 0;*out=(ThemeRecord){(uint8_t)id,(uint8_t)type,(uint8_t)x,(uint8_t)y,(uint8_t)orientation,(uint8_t)monster};return 1;
}
static void theme_propose(const Game *g,const ThemeRecord *r,uint32_t *seed){
 ThemeScratch *t=theme_scratch;unsigned family=(dungeon_level-1)/4,type=r->type;int x=r->x,y=r->y,id=r->room;
 t->proposed_count=t->overflow=0;
 if(type==THEME_SHRINE||type==THEME_LIBRARY){
  unsigned kind=type==THEME_SHRINE?(r->orientation?PROP_SHRINE_R:PROP_SHRINE_L):(r->orientation?PROP_BOOKCASE_R:PROP_BOOKCASE_L);
  theme_add(g,x,y,kind,0);theme_add(g,x-(r->orientation?1:0),y-(r->orientation?0:1),PROP_THEME_CANDLE,0);theme_add(g,x+(r->orientation?1:0),y+(r->orientation?0:1),PROP_THEME_CANDLE,0);
 }
 static const uint8_t fountain_kind[5]={PROP_BLOOD_FOUNTAIN,PROP_PURIFYING_FOUNTAIN,PROP_CAULDRON,PROP_MURKY_FOUNTAIN,PROP_TEAR_FOUNTAIN};
 if(type==THEME_BLOODFOUNTAIN||type==THEME_PURIFYINGFOUNTAIN||type==THEME_CAULDRON||type==THEME_MURKYFOUNTAIN||type==THEME_TEARFOUNTAIN){
  unsigned n=type==THEME_BLOODFOUNTAIN?0:type==THEME_PURIFYINGFOUNTAIN?1:type==THEME_CAULDRON?2:type==THEME_MURKYFOUNTAIN?3:4;theme_add(g,x,y,fountain_kind[n],0);
 }
 if(type==THEME_GOATSHRINE)theme_add(g,x,y,PROP_GOAT_SHRINE,0);
 if(type==THEME_SKELROOM){
  theme_add(g,x,y,PROP_SKEL_FIRE,0);
  static const int8_t dx[6]={-1,1,-1,1,-1,1},dy[6]={-1,-1,0,0,1,1};
  static const uint8_t banner[6]={PROP_THEME_BANNER_L,PROP_THEME_BANNER_R,PROP_THEME_BANNER_M,PROP_THEME_BANNER_M,PROP_THEME_BANNER_R,PROP_THEME_BANNER_L};
  for(unsigned n=0;n<6;n++)if(!theme_random(seed,family?7:6))theme_add(g,x+dx[n],y+dy[n],banner[n],0);
  if(!theme_occupied(g,x,y-3))theme_add(g,x,y-2,PROP_SKEL_BOOK,0);
  if(!theme_occupied(g,x,y+3))theme_add(g,x,y+2,PROP_SKEL_BOOK,0);
 }
 if(type==THEME_ARMORSTAND&&!t->armor)theme_add(g,x,y,PROP_ARMOR_STAND,0);
 if(type==THEME_WEAPONRACK&&!t->weapon)theme_add(g,x,y,PROP_WEAPON_RACK,0);
 static const uint8_t barrel[4]={2,6,4,8},scatter[4]={6,8,3,8},cross[4]={5,7,3,8},weapon[4]={6,8,5,8};
 for(y=1;y<79&&!t->overflow;y++)for(x=1;x<79&&!t->overflow;x++){
  if(theme_room(x,y)!=id||(theme_native_flags(x,y)&1))continue;
  if(type==THEME_BARREL){if(!theme_occupied(g,x,y)&&!theme_random(seed,barrel[family]))theme_add(g,x,y,theme_random(seed,barrel[family])?PROP_EXPLOSIVE_BARREL:PROP_BARREL,0);continue;}
  if(type!=THEME_LIBRARY&&type!=THEME_TORTURE&&type!=THEME_DECAPITATED&&type!=THEME_BRNCROSS&&type!=THEME_ARMORSTAND&&type!=THEME_WEAPONRACK)continue;
  if(!theme_obj3(g,x,y,id))continue;
  unsigned freq=type==THEME_LIBRARY?(family?2:1):type==THEME_BRNCROSS?cross[family]:type==THEME_WEAPONRACK?weapon[family]:scatter[family];
  if(theme_random(seed,freq))continue;
  unsigned kind=type==THEME_LIBRARY?PROP_BOOKSTAND:type==THEME_TORTURE?14:type==THEME_DECAPITATED?PROP_THEME_DECAP:type==THEME_BRNCROSS?PROP_BURNING_CROSS:type==THEME_ARMORSTAND?PROP_ARMOR_STAND:PROP_WEAPON_RACK;
  unsigned state=type==THEME_LIBRARY?!!theme_random(seed,2*freq):type==THEME_ARMORSTAND||type==THEME_WEAPONRACK;
  theme_add(g,x,y,kind,state);
 }
}
static int theme_commit(Game *g,unsigned limit){
 ThemeScratch *t=theme_scratch;if(t->overflow)return 0;
 unsigned n=0;for(unsigned i=room_x>=0?13:0;i<limit&&n<t->proposed_count;i++)if(!g->props[i].kind)t->slots[n++]=(uint8_t)i;
 if(n<t->proposed_count)return 0;
 theme_reachable(g,t->before);
 for(unsigned i=0;i<n;i++)g->props[t->slots[i]]=t->proposed[i];
 theme_reachable(g,t->seen);int valid=1;
 for(unsigned c=0;c<6400;c++)if((t->before[c>>3]&(1u<<(c&7)))&&theme_floor(g,c%80,c/80)&&!(t->seen[c>>3]&(1u<<(c&7)))){valid=0;break;}
 if(!valid){for(unsigned i=0;i<n;i++)g->props[t->slots[i]]=(Prop){0};return 0;}
 return 1;
}
static int theme_valid_cathedral(const Game *g,int id){
 (void)g;
 ThemeScratch *t=theme_scratch;if(t->area[id]<9||t->area[id]>100||t->excluded[id])return 0;
 static const int8_t dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
 for(int y=0;y<80;y++)for(int x=0;x<80;x++)if(theme_room(x,y)==id&&!(theme_native_flags(x,y)&1)){
  for(unsigned n=0;n<4;n++)if(theme_room(x+dx[n],y+dy[n])!=id&&!(theme_native_flags(x+dx[n],y+dy[n])&1))return 0;
 }
 return 1;
}
static unsigned theme_population(Game *g,uint32_t *seed,const uint8_t *roster,unsigned types,unsigned limit){
 ThemeScratch *t=theme_scratch;for(unsigned i=0;i<sizeof(*t);i++)((uint8_t*)t)[i]=0;
 if(dungeon_level>=16||!game_rooms_candidate())goto done;
 if(dungeon_level<=4&&!game_dungeon_read_macros(g,(void*)dungeon_workspace))goto done;
 const uint8_t *native=game_rooms_candidate();for(unsigned i=0;i<112*112;i++)t->area[native[i]]++;
 for(int y=0;y<80;y++)for(int x=0;x<80;x++){
  int id=theme_room(x,y);if(id<0)continue;
  if(g->map[y][x]==2||(x==g->down_x&&y==g->down_y)||in_butcher_room(x,y)||game_quest_valor_room(x,y)||
     (dungeon_level==10&&quest_anvil_cell&&x>=quest_anvil_cell%80-12&&x<=quest_anvil_cell%80+12&&y>=quest_anvil_cell/80-12&&y<=quest_anvil_cell/80+12))t->excluded[id]=1;
 }
 static const uint8_t good[4]={THEME_GOATSHRINE,THEME_SHRINE,THEME_SKELROOM,THEME_LIBRARY};
 unsigned candidates=dungeon_level<=4?256:game_world_theme_count();
 /* InitThemes reserves the first fitting native library for Zhar before
  * assigning any other theme. Its exact native actor cell is kept clear. */
 if(dungeon_level==8&&game_campaign_layout()&&quest_progress.state[3]&&quest_progress.state[3]!=3){
  for(unsigned i=0;i<candidates;i++){
   WorldThemeRoom room;ThemeRecord record;if(!game_world_theme_get(i,&room)||t->excluded[room.room])continue;
   if(!theme_fit(g,THEME_LIBRARY,room.room,roster,types,seed,&record))continue;
   t->zhar=1;t->zhar_x=2*room.x+4;t->zhar_y=2*room.y+4;
   theme_propose(g,&record,seed);
   if(!theme_commit(g,limit)){t->zhar=0;t->proposed_count=0;continue;}
   record.monster=254;t->rooms[t->count++]=record;t->held[room.room]=1;break;
  }
 }
 for(unsigned i=0;i<candidates&&t->count<WORLD_THEME_LIMIT;i++){
  t->proposed_count=0;
  WorldThemeRoom room;int id=(int)i;
  if(dungeon_level>4){if(!game_world_theme_get(i,&room))continue;id=room.room;}
  if(t->excluded[id]||t->held[id]||(dungeon_level<=4&&!theme_valid_cathedral(g,id)))continue;
  unsigned first=good[theme_random(seed,4)];
  for(unsigned attempt=0;attempt<64;attempt++){
   t->proposed_count=0;
   unsigned type=attempt?theme_random(seed,17):first;ThemeRecord record;
   if(!theme_fit(g,type,id,roster,types,seed,&record))continue;
   theme_propose(g,&record,seed);if(!theme_commit(g,limit))continue;
   t->rooms[t->count++]=record;t->held[id]=1;t->unique|=1u<<type;
   if(type==THEME_ARMORSTAND)t->armor=1;if(type==THEME_WEAPONRACK)t->weapon=1;break;
  }
 }
done:;
 t->proposed_count=0;
 unsigned used=room_x>=0?13:0;for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind)used=i+1;return used;
}
static void theme_item(Game *g,int x,int y,int gold,int good,uint32_t *seed){
 if(!game_item_space(g,x,y))return;
 uint32_t roll=rnd(seed);unsigned kind=gold?0:item_candidate(roll,2*game_dungeon_depth());
 Item item=item_create(kind,roll);if(!gold)item_generate_magic(&item,2*game_dungeon_depth(),good);
 (void)game_drop_put(g,item_to_drop(&item,(unsigned)x,(unsigned)y));
}
static unsigned theme_monsters(Game *g,uint32_t *seed,const uint8_t *roster,unsigned types,unsigned first,unsigned capacity){
 ThemeScratch *t=theme_scratch;unsigned used=0,family=(dungeon_level-1)/4;
 static const uint8_t freq[17][4]={{5,7,3,9},{6,6,3,9},{6,7,3,9},{0,0,0,0},{6,8,3,7},{5,7,3,9},{6,8,3,9},{6,8,3,9},{6,8,3,9},{6,7,3,9},{6,7,3,9},{0,0,0,0},{6,7,3,9},{6,7,3,9},{6,7,3,9},{6,8,3,9},{6,7,3,9}};
 static const uint8_t treasure[4]={4,9,7,10};
 for(unsigned r=0;r<t->count;r++){
  const ThemeRecord *room=&t->rooms[r];if(room->monster==254)continue;unsigned type=room->type,kind=room->monster==255?roster[theme_random(seed,types)]:room->monster;
  if(type==THEME_MONSTPIT){unsigned take=theme_random(seed,100)+1;int bx=-1,by=0;
   for(unsigned pass=0;take&&pass<100;pass++)for(int y=0;y<80&&take;y++)for(int x=0;x<80&&take;x++)if(theme_room(x,y)==room->room&&!(theme_native_flags(x,y)&1)){if(!--take){bx=x;by=y;}}
   if(bx>=0)theme_item(g,bx,by,0,1,seed);
  }
  for(int y=0;y<80;y++)for(int x=0;x<80;x++){
   if(theme_room(x,y)!=room->room||(theme_native_flags(x,y)&1))continue;
   if(type==THEME_TREASURE){unsigned rv=theme_random(seed,treasure[family]);if(!theme_random(seed,treasure[family]))theme_item(g,x,y,1,0,seed);if(!rv)theme_item(g,x,y,0,0,seed);}
   int special=type==THEME_SKELROOM||type==THEME_GOATSHRINE;
   if(special){int dx=x-room->x,dy=y-room->y;if(dx< -1||dx>1||dy< -1||dy>1||(!dx&&!dy))continue;}
   if((x==g->x&&y==g->y)||!game_walkable(g,x,y)||theme_occupied(g,x,y)||game_monster_blocks(g,x,y,-1))continue;
   int item=0;for(unsigned i=0;i<MAX_DROPS;i++)if(g->drops[i].active&&g->drops[i].x==x&&g->drops[i].y==y){item=1;break;}if(item)continue;
   if(!special&&theme_random(seed,freq[type][family]))continue;
   if(used>=capacity)continue;
   unsigned index=first+used++;g->enemies[index]=(Enemy){x,y,game_monster_hp(kind,rnd(seed)),1,kind,0};g->enemy_facing[index]=(uint8_t)(special?1:theme_random(seed,8));g->enemy_attack[index]=0;
  }
 }
 return used;
}

static void theme_zhar(Game *g){
 ThemeScratch *t=theme_scratch;
 if(dungeon_level!=8||!game_campaign_layout()||!quest_progress.state[3]||quest_progress.state[3]==3)return;
 /* Native PlaceUniqueMonst retains its initial random position when no
  * library was selected. Keep the safe quest_place fallback in that case.
  * A fitting library replaces the actor position, never adds a second one. */
 if(!t->zhar)return;
 g->enemies[MAX_ENEMIES-2]=(Enemy){t->zhar_x,t->zhar_y,game_monster_hp(MONSTER_ZHAR,campaign_seed),1,MONSTER_ZHAR,0};
}
