/* Regional door terrain and population. AddL2Objs/AddL3Objs inspect native
 * dPiece after the 16-cell border; the port has already cropped that border.
 * No coordinate offset belongs here. All piece constants are native ID - 1. */
static unsigned world_door_region(void){
 if((dungeon_level>=5&&dungeon_level<=8)||dungeon_level==REGION_BONE)return 2;
 if((dungeon_level>=9&&dungeon_level<=12)||dungeon_level==REGION_WATER)return 3;
 return 0;
}
static unsigned world_door_kind(unsigned p){
 unsigned region=world_door_region();
 if(region==2){if(p==12||p==540)return PROP_L2_DOOR_L;if(p==16||p==541)return PROP_L2_DOOR_R;}
 if(region==3){if(p==530)return PROP_L3_DOOR_L;if(p==533)return PROP_L3_DOOR_R;}
 return 0;
}
static void world_door_add(Game *g,int x,int y,unsigned piece_id){
 unsigned kind=world_door_kind(piece_id);if(!kind||x<0||y<0||x>=80||y>=80)return;
 int empty=-1;unsigned limit=dungeon_level<=12?MAX_PROPS-2:MAX_PROPS;
 for(unsigned i=0;i<MAX_PROPS;i++){
  const Prop *p=&g->props[i];
  if(p->kind&&p->x==x&&p->y==y)return; /* Never replace a quest or loot object. */
  if(!p->kind&&i<limit&&empty<0)empty=(int)i;
 }
 if(empty<0)return; /* A full prop archive keeps the former open passage. */
 /* Fixed quest populations precede this pass. Keep an occupied door open
  * instead of embedding a living actor inside a newly closed obstacle. */
 unsigned open=g->x==x&&g->y==y;
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive&&g->enemies[i].x==x&&g->enemies[i].y==y)open=1;
 g->props[empty]=(Prop){x,y,kind,open};
 if(g->map[y][x]!=2)g->map[y][x]=1; /* Closed collision is supplied by the prop. */
}
unsigned game_world_doors_prepared(Game *g){
 unsigned region=world_door_region();
 if(region){
  const WorldArt *a=game_world_art(dungeon_level);
  for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++){
   unsigned mx=x/2,my=y/2,macro;
   if(dungeon_level==REGION_BONE){
    macro=12;
    if(mx<32&&my<18){
     const uint8_t *m=late_bonecha1;
     if(((quest_progress.bone_gates&1)&&mx>=17&&mx<=21&&my<=5)||((quest_progress.bone_gates&2)&&mx>=13&&mx<=16&&my<=5))m=late_bonecha2;
     macro=m[4+2*(my*32+mx)];if(!macro)macro=3;
    }
   }else if(dungeon_level==REGION_WATER){macro=mx<19&&my<37?water_macros[my*19+mx]:8;if(!macro)macro=7;}
   else macro=dungeon_workspace[my*40+mx];
   unsigned p=macro&&macro<=a->macro_count?a->macros[(macro-1)*4+(y&1)*2+(x&1)]:0;
   world_door_add(g,(int)x,(int)y,p);
  }
 }
 unsigned used=0;for(unsigned i=0;i<MAX_PROPS;i++)used+=g->props[i].kind!=0;return used;
}
unsigned game_world_doors(Game *g,uint32_t seed){
 if(world_door_region()&&dungeon_level<=12){int up[2],down[2];layout(seed,up,down);}
 return game_world_doors_prepared(g);
}
void game_world_door_overlay(const Game *g,unsigned index,uint16_t *tiles,uint8_t *depth){
 if(!g||!tiles||index>=MAX_PROPS)return;
 unsigned region=world_door_region();if(!region)return;
 const WorldArt *a=game_world_art(dungeon_level);
  const Prop *door=&g->props[index];unsigned kind=door->kind;
  if(door->x>=80||door->y>=80)return;
  if(region==2&&(kind!=PROP_L2_DOOR_L&&kind!=PROP_L2_DOOR_R))return;
  if(region==3&&(kind!=PROP_L3_DOOR_L&&kind!=PROP_L3_DOOR_R))return;
  unsigned cell=door->y*80+door->x,p=tiles[cell],left=kind==PROP_L2_DOOR_L||kind==PROP_L3_DOOR_L;
  unsigned closed=region==2?(left?537:539):(left?530:533),opened=region==2?(left?12:16):(left?537:540);
  /* Quest seals own their walls. A stale record must never paint over a seal. */
  if(p!=closed&&p!=opened&&!(region==2&&p==(left?540u:541u)))return;
  p=door->state?opened:closed;tiles[cell]=(uint16_t)p;if(depth)depth[cell]=a->occludes[p];
}
void game_world_doors_overlay(const Game *g,uint16_t *tiles,uint8_t *depth){
 for(unsigned i=0;i<MAX_PROPS;i++)game_world_door_overlay(g,i,tiles,depth);
}
