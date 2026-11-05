#include "effect_tiles.h"

static EffectTileCache effect_tile_cache;
/* Cumulative 128-byte image transfers, also available to emulator probes. */
volatile unsigned effect_tile_uploads;

static UI_FN void effect_tiles_invalidate(void){
 for(unsigned i=0;i<EFFECT_TILE_SLOTS;i++)effect_tile_cache.keys[i]=0;
 effect_tile_cache.pinned=0;
}
/* Fixed per-frame atlas plan: large images occupy four contiguous blocks,
 * small images one. Every OAM is submitted only after this plan is complete. */
static UI_FN unsigned effect_tiles_place(const uint32_t *pixels,unsigned slot,unsigned blocks){
 unsigned tile=effect_tile_address(slot),changed=0;
 for(unsigned j=0;j<blocks;j++)if(effect_tile_cache.keys[slot+j]!=pixels+j*32)changed=1;
 if(changed){
  if(game.area){dma(pixels,(void*)(uintptr_t)(0x06010000u+tile*32u),blocks*EFFECT_TILE_BYTES);effect_tile_uploads+=blocks;}
  else for(unsigned j=slot;j<slot+blocks;j++){
   effect_tile_cache.pinned|=(uint16_t)(1u<<(j/4));
   if(j>=16){unsigned actor=11+(j-16)/16;DEATH_SPRITE_STATE->pending|=(uint16_t)(1u<<actor);D_STAGE_TILES[actor][0]=D_STAGE_TILES[actor][1]=~0u;}
  }
 }
 for(unsigned j=0;j<blocks;j++)effect_tile_cache.keys[slot+j]=pixels+j*32;
 return tile;
}

/* Immutable ROM pixels need no extra staging arena. 'pinned' is the dirty
 * four-block group mask; actor slots11/12 share the scan-following scheduler. */
static UI_FN void effect_tiles_commit_range(unsigned first,unsigned limit){
 for(unsigned i=first;i<limit;){
  if(!(effect_tile_cache.pinned&(1u<<(i/4)))||!effect_tile_cache.keys[i]){i++;continue;}
  unsigned end=i+1,tile=effect_tile_address(i);
  while(end<limit&&(effect_tile_cache.pinned&(1u<<(end/4)))&&
        (uintptr_t)effect_tile_cache.keys[end]==(uintptr_t)effect_tile_cache.keys[i]+(end-i)*128&&
        effect_tile_address(end)==tile+(end-i)*4)end++;
  dma(effect_tile_cache.keys[i],(void*)(uintptr_t)(0x06010000u+tile*32u),(end-i)*128);
  effect_tile_uploads+=end-i;i=end;
 }
 for(unsigned group=first/4;group<limit/4;group++)effect_tile_cache.pinned&=(uint16_t)~(1u<<group);
}
static UI_FN unsigned effect_tiles_base_budget(void){
 unsigned blocks=0;
 for(unsigned i=0;i<16;i++)if((effect_tile_cache.pinned&(1u<<(i/4)))&&effect_tile_cache.keys[i])blocks++;
 return blocks?2+(blocks+1)/2:0;
}
