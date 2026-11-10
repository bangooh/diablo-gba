/* Validate archived fields before any of them reach the live Game.
 * Read bytewise: packed archive fields need not be naturally aligned.
 * Prop unpacking already bounds both coordinate-escape representations. */
static UI_FN unsigned floor_middle_byte(const FloorState *src,unsigned offset){
 unsigned facing=offsetof(Game,enemy_facing);
 if(offset>=facing+MAX_ENEMIES)offset-=MAX_ENEMIES;
 return src->middle[offset-FLOOR_MIDDLE_OFFSET];
}
static UI_FN int floor_middle_i16(const FloorState *src,unsigned offset){
 unsigned value=floor_middle_byte(src,offset)|(floor_middle_byte(src,offset+1)<<8);
 return value>=32768?(int)value-65536:(int)value;
}
static UI_FN int floor_state_valid(const FloorState *src){
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const SavedEnemy *e=&src->enemies[i];
  unsigned position=e->position[0]|((unsigned)e->position[1]<<8);
  if((position&127)>=MAP_W||((position>>7)&127)>=MAP_H||
     (position>>14)>2||e->kind>=MONSTER_TYPES)return 0;
 }
 for(unsigned i=0;i<MAX_DROPS;i++){
  const uint8_t *p=src->drops[i].position;
  unsigned bits=p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16);
  if((bits&127)>=MAP_W||((bits>>7)&127)>=MAP_H||(bits>>23)||
     ((bits&(1u<<14))&&((bits>>15)&255)>=ITEM_COUNT))return 0;
 }
 int x=floor_middle_i16(src,offsetof(Game,x));
 int y=floor_middle_i16(src,offsetof(Game,y));
 if(x<0||x>=MAP_W||y<0||y>=MAP_H)return 0;
 if(floor_middle_byte(src,offsetof(Game,area))!=0||
    floor_middle_byte(src,offsetof(Game,facing))>7||
    floor_middle_byte(src,offsetof(Game,won))>1||
    floor_middle_byte(src,offsetof(Game,paused))>1)return 0;
 /* Negative values encode props, portals and spell actions, not enemies. */
 unsigned target=floor_middle_byte(src,offsetof(Game,target));
 unsigned swing=floor_middle_byte(src,offsetof(Game,swing_target));
 if((target<128&&target>=MAX_ENEMIES)||(swing<128&&swing>=MAX_ENEMIES))return 0;
 if(src->tail[offsetof(Game,down_x)-FLOOR_END_OFFSET]>=MAP_W||
    src->tail[offsetof(Game,down_y)-FLOOR_END_OFFSET]>=MAP_H)return 0;
 return 1;
}
