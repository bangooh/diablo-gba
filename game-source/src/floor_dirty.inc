/* FloorDraw stays8B: foot8 is the world foot divided by8, id is the
 * persistent corpse/drop array index. Preparation emits strictly ascending
 * ids, so a merge preserves painter order without matching unrelated images. */
_Static_assert(sizeof(FloorDraw)==8,"floor draw scratch must remain8B per entry");
_Static_assert(MAX_ENEMIES+MAX_DROPS<=256,"floor source id must fit one byte");
/* foot =72+(cell_x+cell_y)*8, including the10px quest-item lift reversal. */
_Static_assert(72/8+MAP_W-1+MAP_H-1<=255,"floor foot/8 must fit one byte");
static UI_FN void floor_draw_invalidate(const FloorDraw *d){
 floor_invalidate(d);
 death_sprite_invalidate_rect(d->x,d->y,64,64);
}
static UI_FN void floor_list_invalidate(const FloorDraw *out,unsigned n,const FloorDraw *old,unsigned old_n){
 unsigned i=0,j=0;
 while(i<n||j<old_n){
  if(i==n||(j<old_n&&old[j].id<out[i].id)){floor_draw_invalidate(&old[j++]);continue;}
  if(j==old_n||out[i].id<old[j].id){floor_draw_invalidate(&out[i++]);continue;}
  const FloorDraw *a=&old[j++],*b=&out[i++];
  if(a->x==b->x&&a->y==b->y&&a->foot8==b->foot8&&a->frame==b->frame)continue;
  floor_draw_invalidate(a);floor_draw_invalidate(b);
 }
}
