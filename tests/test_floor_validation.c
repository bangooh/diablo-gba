/* Included after the production archive/Game declarations by test_core.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "floor_validate.inc"
static void middle(FloorState *s,unsigned offset,unsigned value){
 if(offset>=offsetof(Game,enemy_facing)+MAX_ENEMIES)offset-=MAX_ENEMIES;
 s->middle[offset-FLOOR_MIDDLE_OFFSET]=value;
}
int main(void){
 FloorState good={0},s;assert(floor_state_valid(&good));
 s=good;s.enemies[0].position[0]=80;assert(!floor_state_valid(&s));
 s=good;s.enemies[0].position[1]=40;assert(!floor_state_valid(&s));
 s=good;s.enemies[0].position[1]=192;assert(!floor_state_valid(&s));
 s=good;s.enemies[0].kind=MONSTER_TYPES;assert(!floor_state_valid(&s));
 s=good;s.drops[0].position[0]=127;assert(!floor_state_valid(&s));
 s=good;s.drops[0].position[1]=40;assert(!floor_state_valid(&s));
 if(ITEM_COUNT<256){
  s=good;unsigned bits=(1u<<14)|((unsigned)ITEM_COUNT<<15);
  for(unsigned j=0;j<3;j++)s.drops[0].position[j]=bits>>(8*j);
  assert(!floor_state_valid(&s));
 }
 s=good;s.drops[0].position[2]=128;assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,x),80);assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,y)+1,255);assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,area),1);assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,facing),8);assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,target),MAX_ENEMIES);assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,swing_target),127);assert(!floor_state_valid(&s));
 for(unsigned i=128;i<256;i++){
  s=good;middle(&s,offsetof(Game,target),i);middle(&s,offsetof(Game,swing_target),i);
  assert(floor_state_valid(&s)); /* Keep negative spell/prop sentinels. */
 }
 s=good;s.tail[0]=80;assert(!floor_state_valid(&s));
 s=good;s.tail[1]=127;assert(!floor_state_valid(&s));
 s=good;middle(&s,offsetof(Game,x),79);middle(&s,offsetof(Game,y),79);
 middle(&s,offsetof(Game,facing),7);s.tail[0]=s.tail[1]=79;
 assert(floor_state_valid(&s));
 puts("PASS: floor archive invalid coordinates, indices, flags and sentinel boundaries");
}
