/* Town terrain occupies the whole scene arena. Composite resting items into
 * streamed BG tiles, using the otherwise idle upper load-buffer range. This
 * avoids stealing NPC/effect OBJ slots and works with forty town drops. */
typedef struct {int16_t x,y;uint8_t image,selected;} TownItemDraw;
/* Only the first1152 collision bytes are live in town. The dungeon reuses
 * the full arena for corpses, so load_scene explicitly resets this cache. */
typedef struct {unsigned palette_ready;TownItemDraw items[MAX_DROPS+5];uint8_t palette[2][16];} TownItemCache;
#define TOWN_ITEM_CACHE ((TownItemCache*)(town_collision+1152))
_Static_assert(1152+sizeof(TownItemCache)<=sizeof(town_collision),"town item cache overlaps collision");
static UI_FN void town_items_reset(void){TOWN_ITEM_CACHE->palette_ready=0;for(unsigned i=0;i<MAX_DROPS+5;i++)TOWN_ITEM_CACHE->items[i].x=RING_INVALID;}
static UI_FN void town_item_invalidate(const TownItemDraw *d){
 unsigned width=ground_item_wide[d->image]?32:16;
 for(int y=render_tile(d->y);y<=render_tile(d->y+15);y++)for(int x=render_tile(d->x);x<=render_tile(d->x+width-1);x++){
  unsigned slot=render_slot(x,y);
  if(ring_x[slot]==x&&ring_y[slot]==y)terrain_invalidate(slot);
 }
}
static inline void town_item_forget(unsigned i){
 TownItemDraw *old=&TOWN_ITEM_CACHE->items[i];
 if(old->x!=RING_INVALID){town_item_invalidate(old);old->x=RING_INVALID;}
}
_Static_assert(12288+GROUND_ITEM_BYTES<=sizeof(load_buffer),"town item scratch capacity");
static UI_FN unsigned town_items_prepare(TownItemDraw list[TOWN_DROPS+5]){
 unsigned n=0;
 highlighted_drop=game_item_target(&game);
 for(unsigned i=0;i<MAX_DROPS+5;i++){
  const Drop *d=i<MAX_DROPS?&game.drops[i]:i==MAX_DROPS?&quest_progress.reward:i==MAX_DROPS+1?&quest_progress.reward_anvil:i==MAX_DROPS+2?&quest_progress.reward_banner:i==MAX_DROPS+3?&quest_progress.reward_water:&quest_progress.reward_mushroom;
  if(!d->active||d->kind>=ITEM_COUNT){town_item_forget(i);continue;}
  /* Quest pending records can also name an ordinary ground record. */
  if(i>=MAX_DROPS){unsigned duplicate=0;for(unsigned j=0;j<MAX_DROPS;j++)if(game.drops[j].active&&game.drops[j].x==d->x&&game.drops[j].y==d->y){duplicate=1;break;}if(duplicate){town_item_forget(i);continue;}}
  unsigned selected=i==(unsigned)highlighted_drop;
  int x=1536+((int)d->x-d->y)*16,y=128+((int)d->x+d->y)*8;
  if(x+16<=cam_x||x-16>=cam_x+240||y<=cam_y||y-16>=cam_y+160||n>=TOWN_DROPS+5){town_item_forget(i);continue;}
  Item it=item_from_drop(d);unsigned image=ground_item_image[item_art_index(&it)];
  TownItemDraw draw={(int16_t)(x-(ground_item_wide[image]?16:8)),(int16_t)(y-15),(uint8_t)image,(uint8_t)selected};
  TownItemDraw *old=&TOWN_ITEM_CACHE->items[i];
  if(old->x!=draw.x||old->y!=draw.y||old->image!=draw.image||old->selected!=draw.selected){
   if(old->x!=RING_INVALID)town_item_invalidate(old);
   town_item_invalidate(&draw);*old=draw;
  }
  unsigned at=n++;while(at&&list[at-1].y>draw.y){list[at]=list[at-1];at--;}list[at]=draw;
 }
 /* Each persistent record keeps its own footprint, so entering the viewport
  * never shifts cache identities or forces the other items to be redrawn. */
 return n;
}
static UI_FN void town_items_palette(uint8_t lut[2][16]){
 if(TOWN_ITEM_CACHE->palette_ready){for(unsigned i=0;i<32;i++)((uint8_t*)lut)[i]=((uint8_t*)TOWN_ITEM_CACHE->palette)[i];return;}
 for(unsigned selected=0;selected<2;selected++)for(unsigned c=0;c<16;c++){
  if(c<6){lut[selected][c]=0;continue;}
  unsigned rgb=base_palette[256+15*16+c],best=~0u,at=1;
  int r=rgb&31,g=(rgb>>5)&31,b=rgb>>10;
  if(selected){r+=(31-r)/3;g+=(31-g)/3;b+=(31-b)/3;}
  for(unsigned j=1;j<256;j++){
   unsigned color=base_palette[j];int dr=(int)(color&31)-r,dg=(int)((color>>5)&31)-g,db=(int)(color>>10)-b;
   unsigned distance=dr*dr+dg*dg+db*db;if(distance<best){best=distance;at=j;}
  }
  lut[selected][c]=(uint8_t)at;
 }
 for(unsigned i=0;i<32;i++)((uint8_t*)TOWN_ITEM_CACHE->palette)[i]=((uint8_t*)lut)[i];TOWN_ITEM_CACHE->palette_ready=1;
}
static UI_FN void town_items_overlay(const TownItemDraw *list,unsigned count,int px,int py,unsigned slot,uint8_t *pixels,unsigned *ready,uint8_t lut[2][16]){
 uint8_t *atlas=(uint8_t*)load_buffer+12288;
 for(unsigned i=0;i<count;i++){
  const TownItemDraw *d=&list[i];unsigned width=ground_item_wide[d->image]?32:16;
  int x=px-d->x,y=py-d->y;if(x>=(int)width||x+8<=0||y>=16||y+8<=0)continue;
  if(!*ready){lz_decode(asset_ground_items,atlas,GROUND_ITEM_BYTES);town_items_palette(lut);*ready=1;}
  const uint8_t *source=atlas+(ground_item_tiles[d->image]-832)*32;
  for(unsigned yy=0;yy<8;yy++)for(unsigned xx=0;xx<8;xx++){
   int sx=x+(int)xx,sy=y+(int)yy;if(sx<0||sx>=(int)width||sy<0||sy>=16)continue;
   unsigned at=(sy/8)*(width/8)*32+(sx/8)*32+(sy&7)*4+(sx&7)/2,c=(source[at]>>((sx&1)*4))&15,z=town_depth[slot][yy*8+xx];
   if(c&&(!z||py+(int)z<=d->y+15))pixels[yy*8+xx]=lut[d->selected][c];
  }
 }
}
