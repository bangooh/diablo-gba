#ifndef DUNGEON_DOORS_H
#define DUNGEON_DOORS_H
/* OperateL1[L/R]Door + DoorSet, retail objects.cpp. Input atlas is immutable:
 * deriving from current props makes load/revisit and either opening order agree.
 * objects_set_door_piece restores MIN words 8/9; our intact MIN atlas already
 * contains these words (there is no mutable dpiece cache to repair). */
static unsigned dungeon_open_door(const Game *g,const uint8_t *lookup,int x,int y,unsigned kind){
 if(x<0||y<0||x>=MAP_W||y>=MAP_H)return 0;
 unsigned p=lookup[y*MAP_W+x];
 return p&&p<=MAX_PROPS&&g->props[p-1].state&&g->props[p-1].kind==kind;
}
static unsigned dungeon_door_variant(const Game *g,const uint8_t *lookup,unsigned variant,int x,int y){
 if(variant>=DART_COUNT)return variant;
 /* Most cells cannot be changed by any door; keep the stream hot path cheap. */
 if(!dart_door[variant]&&dart_door_seam_0[variant]==variant)return variant;
 if(dungeon_open_door(g,lookup,x,y,PROP_DOOR_L))return variant==DART_P214?DART_P408:DART_P393;
 if(dungeon_open_door(g,lookup,x,y,PROP_DOOR_R))return DART_P395;
 if(dungeon_open_door(g,lookup,x,y+1,PROP_DOOR_L))variant=dart_door_seam_0[variant];
 if(dungeon_open_door(g,lookup,x+1,y,PROP_DOOR_R))variant=dart_door_seam_1[variant];
 return variant;
}
#endif
