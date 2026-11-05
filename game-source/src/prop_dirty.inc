/* Include after invalidate_prop and death_sprite_invalidate_rect. Color changes
 * rebuild only their terrain tiles; actor masks depend on opacity/depth only. */
#include "../generated/prop_dirty.h"
#include "../generated/environment_prop_dirty.h"
#include "../generated/fountain_prop_dirty.h"
#include "../generated/environment76.h"
#include "../generated/environment76_dirty.h"
static UI_FN void prop_mask_rect(int x,int y,int width,int height){
 ++crowd_mask_epoch;
 for(unsigned actor=0;actor<13;actor++)if(mask_x[actor]<x+width&&mask_x[actor]+64>x&&mask_y[actor]<y+height&&mask_y[actor]+64>y)last_frame[actor]=-1;
 death_sprite_invalidate_rect(x,y,width,height);
}
static UI_FN void prop_frame_changed(int index,unsigned before,unsigned after){
 if(index<0||index>=MAX_PROPS||before==after)return;
 const Prop *p=&game.props[index];
 /* Door state changes collision via the prop, not game.map. Only its art
  * anchor/occlusion byte changes; leave every other prepared wall untouched.
  * Quest/map topology changes still use the full quest_redraw path. */
 if(p->kind>=PROP_L2_DOOR_L&&p->kind<=PROP_L3_DOOR_R){
  game_world_door_overlay(&game,(unsigned)index,town_map,(uint8_t*)town_map+12800);
  fixed_world_wall_cell(p->x,p->y,game_world_art(dungeon_level)->height/16);
 }
 int x=DUNGEON_ORIGIN+((int)p->x-p->y)*16-32,y=72+(p->x+p->y)*8-64;
 if(p->kind==PROP_BURNING_CROSS){x+=8;y-=8;}
 unsigned a=before-PROP_WATER_CANDLE_BASE,b=after-PROP_WATER_CANDLE_BASE;
 const PropFrameDamage *damage=0;
 if((p->kind==PROP_WATER_CANDLE||p->kind==PROP_THEME_CANDLE)&&a<PROP_WATER_CANDLE_FRAMES&&b<PROP_WATER_CANDLE_FRAMES)damage=&candle_frame_damage[a][b];
 a=before-PROP_BLOOD_FOUNTAIN_BASE;b=after-PROP_BLOOD_FOUNTAIN_BASE;
 if(p->kind==PROP_BLOOD_FOUNTAIN&&a<PROP_BLOOD_FOUNTAIN_FRAMES&&b<PROP_BLOOD_FOUNTAIN_FRAMES)damage=&blood_fountain_frame_damage[a][b];
 a=before-PROP_PURIFYING_FOUNTAIN_BASE;b=after-PROP_PURIFYING_FOUNTAIN_BASE;
 if(p->kind==PROP_PURIFYING_FOUNTAIN&&a<PROP_PURIFYING_FOUNTAIN_FRAMES&&b<PROP_PURIFYING_FOUNTAIN_FRAMES)damage=&purifying_fountain_frame_damage[a][b];
 a=before-PROP_MURKY_FOUNTAIN_BASE;b=after-PROP_MURKY_FOUNTAIN_BASE;
 if(p->kind==PROP_MURKY_FOUNTAIN&&a<PROP_MURKY_FOUNTAIN_FRAMES&&b<PROP_MURKY_FOUNTAIN_FRAMES)damage=&murky_fountain_frame_damage[a][b];
 a=before-PROP_TEAR_FOUNTAIN_BASE;b=after-PROP_TEAR_FOUNTAIN_BASE;
 if(p->kind==PROP_TEAR_FOUNTAIN&&a<PROP_TEAR_FOUNTAIN_FRAMES&&b<PROP_TEAR_FOUNTAIN_FRAMES)damage=&tear_fountain_frame_damage[a][b];
 a=before-PROP_SKEL_FIRE_BASE;b=after-PROP_SKEL_FIRE_BASE;
 if(p->kind==PROP_SKEL_FIRE&&a<PROP_SKEL_FIRE_FRAMES&&b<PROP_SKEL_FIRE_FRAMES)damage=&skel_fire_frame_damage[a][b];
 a=before-PROP_GOAT_SHRINE_BASE;b=after-PROP_GOAT_SHRINE_BASE;
 if(p->kind==PROP_GOAT_SHRINE&&a<PROP_GOAT_SHRINE_FRAMES&&b<PROP_GOAT_SHRINE_FRAMES)damage=&goat_shrine_frame_damage[a][b];
 a=before-PROP_BURNING_CROSS_BASE;b=after-PROP_BURNING_CROSS_BASE;
 if(p->kind==PROP_BURNING_CROSS&&a<PROP_BURNING_CROSS_FRAMES&&b<PROP_BURNING_CROSS_FRAMES)damage=&burning_cross_frame_damage[a][b];
 a=before-PROP_BRAZIER_BASE;b=after-PROP_BRAZIER_BASE;
 if(p->kind==PROP_BRAZIER&&a<PROP_BRAZIER_FRAMES&&b<PROP_BRAZIER_FRAMES)damage=&brazier_frame_damage[a][b];
 a=before-PROP_BARREL_BASE;b=after-PROP_BARREL_BASE;
 if(p->kind==PROP_BARREL&&a<PROP_BARREL_FRAMES&&b<PROP_BARREL_FRAMES)damage=&barrel_frame_damage[a][b];
 a=before-PROP_BARRELEX_BASE;b=after-PROP_BARRELEX_BASE;
 if(p->kind==PROP_EXPLOSIVE_BARREL&&a<PROP_BARRELEX_FRAMES&&b<PROP_BARRELEX_FRAMES)damage=&explosive_barrel_frame_damage[a][b];
 a=before-PROP_FLAME_BASE;b=after-PROP_FLAME_BASE;
 if(game_prop_is_flame(p->kind)&&a<PROP_FLAME_FRAMES&&b<PROP_FLAME_FRAMES)damage=&flame_frame_damage[a][b];
 if(damage){
  /* Props stay aligned to8px. Rebuild changed color tiles, and only retire
   * actor masks where alpha/depth can change; all skipped-frame pairs exist. */
  for(unsigned row=0;row<8;row++){
   unsigned shift=(row&3)*8,colors=(damage->color[row>>2]>>shift)&255,opacity=(damage->opacity[row>>2]>>shift)&255;
   if(!colors)continue;
   for(unsigned col=0;col<8;col++)if(colors&(1u<<col)){
    int px=x+(int)col*8,py=y+(int)row*8,tx=render_tile(px),ty=render_tile(py);unsigned slot=render_slot(tx,ty);
    if(ring_x[slot]==tx&&ring_y[slot]==ty){terrain_invalidate(slot);ring_y[slot]=RING_INVALID;}
    if(opacity&(1u<<col))prop_mask_rect(px,py,8,8);
   }
  }
  return;
 }
 /* Initial frames and interactive objects retain their existing geometry
  * bounds, but only overlapping actor masks can be affected by their depth. */
 invalidate_prop(index);
 int width=64,height=64;
 if(game_prop_is_door(p->kind)){x-=16;y-=24;width=96;height=88;}
 prop_mask_rect(x,y,width,height);
}
