/* Retail InitObjects placement: story books on4/8/12, slain hero on9.
 * Flame assets remain supported for existing saves/authored maps, but the
 * unused retail flame system is not injected into random Cathedral floors. */
static unsigned remaining_reserve(void){
 unsigned n=(dungeon_level==4||dungeon_level==8||dungeon_level==12)?7:0;
 if(dungeon_level==9&&!coop_active&&!coop_campaign_active)n++;
 return n;
}
static int remaining_clear(const Game *g,int x,int y,int rx,int ry){
 for(int dy=-ry;dy<=ry;dy++)for(int dx=-rx;dx<=rx;dx++){
  int xx=x+dx,yy=y+dy;
  if((unsigned)xx>=80||(unsigned)yy>=80||g->map[yy][xx]!=1||theme_reserved(xx,yy)||theme_occupied(g,xx,yy)||game_wall_trap_protected(xx,yy))return 0;
  if((xx==g->x&&yy==g->y)||(xx==g->down_x&&yy==g->down_y))return 0;
 }
 return 1;
}
static int remaining_place(Game *g,unsigned kind,unsigned limit,uint32_t *seed){
 for(unsigned attempt=0;attempt<20000;attempt++){
  int x=4+rnd(seed)%72,y=4+rnd(seed)%72;
  if(!remaining_clear(g,x,y,kind==PROP_SLAIN_HERO?2:3,2))continue;
  theme_scratch->proposed_count=theme_scratch->overflow=0;
  theme_add(g,x,y,kind,0);
  if(kind==PROP_STORY_BOOK){
   static const int8_t candle[6][2]={{-2,1},{-2,0},{-1,-1},{1,-1},{2,0},{2,1}};
   for(unsigned n=0;n<6;n++)theme_add(g,x+candle[n][0],y+candle[n][1],PROP_THEME_CANDLE,0);
  }else if(kind==PROP_FLAME_LEVER){
   /* Lever at x,y; trigger strip at y+2, clear of the lever interaction cell. */
   for(int dx=-1;dx<=1;dx++)theme_add(g,x+dx,y+2,PROP_FLAME_X,0);
  }
  if(theme_commit(g,limit)){theme_scratch->proposed_count=0;return 1;}
  theme_scratch->proposed_count=0;
 }
 return 0;
}
static unsigned remaining_population(Game *g,unsigned limit,uint32_t seed){
 /* Separate RNG stream leaves unrelated container generation stable. */
 seed^=0x52454d41u;
 if(dungeon_level==4||dungeon_level==8||dungeon_level==12)remaining_place(g,PROP_STORY_BOOK,limit,&seed);
 if(dungeon_level==9&&!coop_active&&!coop_campaign_active)remaining_place(g,PROP_SLAIN_HERO,limit,&seed);
 unsigned end=room_x>=0?13:0;for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind)end=i+1;
 return end;
}
