/* Animated corpses use spare 8bpp OBJ space (two actor slots each).
 * In solo play, settled remains and overflow retain the BG compositor.
 * Co-op shows only bounded animations and still omits settled remains.
 * Everything here is presentation-only; corpse lifetime and AI are unchanged. */
/* Co-op never uses the BG corpse compositor. Its fixed two-body arena is
 * beyond actor slots8..12 and the651-byte terrain index, before hero staging.
 * Neither the16KiB terrain stage nor frame-map memory is borrowed. */
enum { COOP_DEATH_LIMIT=2, COOP_DEATH_STAGE_OFFSET=6*2048 };
_Static_assert(5*2048+WORLD_TILES<=COOP_DEATH_STAGE_OFFSET,"corpse/terrain index overlap");
_Static_assert(COOP_DEATH_STAGE_OFFSET+COOP_DEATH_LIMIT*4096<=10*2048,"corpse/hero stage overlap");
static UI_FN uint8_t *death_sprite_buffer(unsigned slot){
 return coop_active?DEATH_SPRITE_STAGE+COOP_DEATH_STAGE_OFFSET+(slot-DEATH_SPRITE_STATE->first)*2048:DEATH_SPRITE_STAGE+(slot-1)*2048;
}
static UI_FN void death_sprite_palette(void){
 if(!DEATH_SPRITES||game.area)return;
 DeathSprites *s=DEATH_SPRITE_STATE;
 s->dirty=0x1fff;s->pending=s->displayed=s->commit_start=s->commit_end=s->overruns=0;
 /* Scene archives can restore corpses between simulation ticks. */
#if COOP_DEATH_SPRITES
 game_animated_corpses=0;
 for(unsigned i=0;i<MAX_ENEMIES;i++)game_animated_corpses+=game_corpses[i].ticks>CORPSE_TICKS;
#endif
 uint8_t *map=DEATH_SPRITE_LUT;map[0]=0;
 for(unsigned i=1;i<256;i++){
  unsigned c=base_palette[i],best=~0u,at=1;
  for(unsigned j=1;j<256;j++){
   /* HUD ramp changes at runtime: corpses must never reference it. */
   if(!(j&15)||(j>=3*16+11&&j<=3*16+14))continue;
   unsigned b=base_palette[256+j];int r=(int)(c&31)-(int)(b&31),g=(int)((c>>5)&31)-(int)((b>>5)&31),v=(int)(c>>10)-(int)(b>>10);
   unsigned distance=r*r+g*g+v*v;if(distance<best){best=distance;at=j;}
  }map[i]=(uint8_t)at;
 }
 for(unsigned band=1;band<4;band++)for(unsigned i=0;i<256;i++)map[band*256+i]=i?map[light_bg[i]]:0;
}
static UI_FN int death_sprite_visible(const Motion *m){
 int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;return x>=-32&&x<=272&&y>=0&&y<=208;
}
static UI_FN void death_sprite_plan(void){
 if(!DEATH_SPRITES||game.area)return;
 DeathSprites *s=DEATH_SPRITE_STATE;
 /* Keep last frame's bodies available for terrain invalidation. The final
  * co-op slot assignment is made after all living actors have been drawn. */
 if(coop_active)return;s->count=0;
 /* Count deaths first: crowded scenes fall back before scanning actor motion. */
 for(unsigned i=0;i<MAX_ENEMIES;i++){
  const Corpse *c=&game_corpses[i];if(c->ticks<=CORPSE_TICKS||c->kind>=MONSTER_TYPES)continue;
  int x=DUNGEON_ORIGIN+((int)c->x-c->y)*16-32,y=72+((int)c->x+c->y)*8-64;
  if(x+64<=cam_x||x>=cam_x+248||y+64<=cam_y||y>=cam_y+168)continue;
  if(s->count==4){s->count=0;return;}
  s->indices[s->count++]=(uint8_t)i;
 }
 if(!s->count)return;
 unsigned live=0;
 for(unsigned i=0;i<=MAX_ENEMIES;i++)if((!i||game.enemies[i-1].alive==1)&&death_sprite_visible(&motion[i]))live++;
 if(coop_active)for(unsigned i=0;i<4;i++)if(i!=coop_id&&(coop_members&(1u<<i))&&death_sprite_visible(&coop_motion[i]))live++;
 /* Slot zero belongs to the hero. The finale camera may leave the hero
  * offscreen, but the atomic staging arena deliberately starts at slot one. */
 if(!live)live=1;if(live>11)live=11;s->first=(uint8_t)live;
 /* A split BG/OBJ death group was slower than the original BG compositor. */
 if(live+s->count*2>11)s->count=0;
}
static UI_FN void death_sprite_coop_plan(unsigned first){
 if(!DEATH_SPRITES||game.area||!coop_active)return;
 if(!first)first=1; /* Slot zero remains reserved for hero staging. */
 DeathSprites *s=DEATH_SPRITE_STATE;s->count=0;s->first=(uint8_t)first;
 if(!COOP_DEATH_SPRITES||first>9||!game_animated_corpses)return;
 unsigned limit=(11-first)/2;if(limit>COOP_DEATH_LIMIT)limit=COOP_DEATH_LIMIT;
 for(unsigned i=0;i<MAX_ENEMIES&&s->count<limit;i++){
  const Corpse *c=&game_corpses[i];if(c->ticks<=CORPSE_TICKS||c->kind>=MONSTER_TYPES)continue;
  int x=DUNGEON_ORIGIN+((int)c->x-c->y)*16-32,y=72+((int)c->x+c->y)*8-64;
  if(x+64<=cam_x||x>=cam_x+240||y+64<=cam_y||y>=cam_y+160)continue;
  s->floor_start[s->count]=0;s->indices[s->count++]=(uint8_t)i;
 }
 /* A newly selected/reassigned body must see every geometry edit made
  * earlier this frame, even if it was not in last frame's invalidate list. */
 if(terrain_upload_count)s->dirty=0x7ff;
}
static UI_FN int death_sprite_index(unsigned corpse){
 if(DEATH_SPRITES&&!game.area)for(unsigned i=0;i<DEATH_SPRITE_STATE->count;i++)if(DEATH_SPRITE_STATE->indices[i]==corpse)return (int)i;
 return -1;
}
/* Dirty bits name OBJ slots, not the current corpse-list order. A terrain or
 * floor edit only invalidates bodies whose previously masked pixels overlap it.
 * New/reassigned slots already miss the asset/frame/position cache below. */
static UI_FN void death_sprite_invalidate_rect(int x,int y,int width,int height){
 if(!DEATH_SPRITES||game.area||width<=0||height<=0)return;
 DeathSprites *s=DEATH_SPRITE_STATE;
 for(unsigned n=0;n<s->count;n++){
  unsigned slot=s->first+n*2;
  if(last_asset[slot]!=asset_floor_decals)continue;
  int wx=mask_x[slot],wy=mask_y[slot];
  if(wx<x+width&&wx+64>x&&wy<y+height&&wy+64>y)s->dirty|=(uint16_t)(1u<<slot);
 }
}
/* Masking clips against the visible part of the terrain cache. Camera motion
 * can reuse a complete body, or a clipped body while its newly visible pixels
 * remain inside the old visible rectangle. Entering pixels require a remask. */
static UI_FN int death_sprite_mask_valid(unsigned slot,int wx,int wy){
 return actor_mask_window_valid(slot,wx,wy);
}
static inline unsigned death_pixel(unsigned x,unsigned y){return (y/8*8+x/8)*64+(y&7)*8+(x&7);}
/* Expand the sparse frame straight into hardware tile order. No linear 4K
 * copy or transpose, and the four BG decode slots remain available to overflow. */
static UI_FN void death_sprite_decode(unsigned frame,uint8_t *out,const uint8_t *lut){
 for(unsigned i=0;i<1024;i++)((uint32_t*)out)[i]=0;
 const uint8_t *picture=asset_floor_decals+((const uint32_t*)asset_floor_decals)[frame+1];
 const uint8_t *body=asset_floor_decals+*(const uint32_t*)(picture+8),*palette=asset_floor_decals+*(const uint32_t*)(picture+4);
 const uint16_t *rows=(const uint16_t*)body;unsigned bits=picture[2],mask=(1u<<bits)-1;
 /* Remap the small source palette once while decoding. Later wall/item
  * masking only depends on transparency, which the remap preserves. */
 uint8_t mapped[256];
 if(lut){for(unsigned i=0;i<=mask;i++)mapped[i]=lut[palette[i]];palette=mapped;}

 for(unsigned row=0;row<picture[1];row++){
  const uint8_t *p=body+rows[row]-8;unsigned col=0,y=row+picture[0],base=(y/8)*512+(y&7)*8;
  for(unsigned run;(run=*p++);){
   if(run>=128){col+=256-run;continue;}
   unsigned repeat=run>64;if(repeat)run-=64;
   for(unsigned i=0;i<run;i++){
    unsigned bit=i*bits,v=repeat?p[0]:p[bit>>3]>>(bit&7);
    if(!repeat&&(bit&7)+bits>8)v|=(unsigned)p[(bit>>3)+1]<<(8-(bit&7));
    unsigned x=col+i;out[base+x/8*64+(x&7)]=palette[v&mask];
   }
   col+=run;p+=repeat?1:(run*bits+7)/8;
  }
 }
}
/* Corpse origins are tile aligned. One ring lookup per 8x8 block avoids
 * software division for every opaque pixel on ARM7. */
/* Two unused living raw-cache slots can retain a decoded, unmasked corpse.
 * Both halves are tagged and pinned: a later living frame may evict either
 * half, which makes the next lookup miss safely. No new RAM is reserved. */
static UI_FN void death_sprite_cached_decode(unsigned frame,uint8_t *out,unsigned slot,unsigned key,const uint8_t *lut){
 unsigned pinned=0;int cached=-1;
 for(unsigned i=0;i<13;i++)if(d_raw_ref[i]<13)pinned|=1u<<d_raw_ref[i];
 for(unsigned i=0;i<12;i++)if(d_raw_base[i]==asset_floor_decals&&d_raw_base[i+1]==asset_floor_decals&&
    d_raw_frame[i]==(int)frame&&d_raw_frame[i+1]==~(int)frame&&d_raw_dir[i]==(int)key){cached=(int)i;break;}
 if(cached<0){
  for(unsigned n=0;n<13;n++){unsigned i=(raw_next+n)%13;if(i<12&&!(pinned&(3u<<i))){cached=(int)i;break;}}
  if(cached<0){death_sprite_decode(frame,out,lut);return;}
  d_raw_base[cached]=d_raw_base[cached+1]=0;
  uint8_t *raw=(uint8_t*)town_map+RAW_DUNGEON_ART+MAP_W*MAP_H*2+(unsigned)cached*2048;
  death_sprite_decode(frame,raw,lut);
  d_raw_frame[cached]=(int)frame;d_raw_frame[cached+1]=~(int)frame;d_raw_dir[cached]=(int)key;
  d_raw_base[cached]=d_raw_base[cached+1]=asset_floor_decals;raw_next=(cached+2)%13;
 }
 d_raw_ref[slot]=(uint8_t)cached;d_raw_ref[slot+1]=(uint8_t)(cached+1);
 const uint8_t *raw=(uint8_t*)town_map+RAW_DUNGEON_ART+MAP_W*MAP_H*2+(unsigned)cached*2048;
 dma(raw,out,2048);dma(raw+2048,out+2048,2048);
}
static UI_FN void death_sprite_mask(uint8_t *pixels,int wx,int wy,int foot,unsigned start){
 for(int ty=0;ty<8;ty++)for(int tx=0;tx<8;tx++){
  int x=render_tile(wx)+tx,y=render_tile(wy)+ty;
  if(x<render_tile(cam_x)||x>render_tile(cam_x+239)||y<render_tile(cam_y)||y>render_tile(cam_y+159)){
   for(unsigned row=0;row<8;row++){uint32_t *p=(uint32_t*)(pixels+(ty*8+tx)*64+row*8);p[0]=p[1]=0;}continue;
  }
  unsigned id=render_slot(x,y);if(depth_max[id]<=(unsigned)foot)continue;
  const uint8_t *z=town_depth[id];
  for(unsigned row=0;row<8;row++)for(unsigned col=0;col<8;col++)if(z[row*8+col]&&y*8+z[row*8+col]>foot)pixels[(ty*8+tx)*64+row*8+col]=0;
 }
 /* Read later BG overlays directly from their sparse row streams. A frame
  * decode per intersecting corpse was more expensive than BG compositing. */
 const FloorDraw *list=FLOOR_LIST(floor_list);
 for(unsigned i=start;i<floor_count[floor_list];i++){
  const FloorDraw *d=&list[i];int dx=d->x-wx,dy=d->y-wy;
  if(dx<=-64||dx>=64||dy<=-64||dy>=64)continue;
  const uint8_t *picture=asset_floor_decals+((const uint32_t*)asset_floor_decals)[d->frame+1];
  const uint8_t *body=asset_floor_decals+*(const uint32_t*)(picture+8),*palette=asset_floor_decals+*(const uint32_t*)(picture+4);
  const uint16_t *rows=(const uint16_t*)body;unsigned bits=picture[2],mask=(1u<<bits)-1;
  for(unsigned row=0;row<picture[1];row++){
   int y=dy+picture[0]+row;if(y<0||y>=64||wy+y<cam_y||wy+y>=cam_y+160)continue;
   const uint8_t *p=body+rows[row]-8;int col=0;
   for(unsigned run;(run=*p++);){
    if(run>=128){col+=256-run;continue;}
    unsigned repeat=run>64;if(repeat)run-=64;
    int left=dx+col,right=left+(int)run;
    if(left<0)left=0;if(right>64)right=64;
    for(int x=left;x<right;x++)if(pixels[death_pixel(x,y)]){
     unsigned bit=(x-dx-col)*bits,v=repeat?p[0]:p[bit>>3]>>(bit&7);
     if(!repeat&&(bit&7)+bits>8)v|=(unsigned)p[(bit>>3)+1]<<(8-(bit&7));
     if(!palette[v&mask])continue;
     if((d->foot8*8)>=foot){pixels[death_pixel(x,y)]=0;continue;}
     int tx=render_tile(wx+x),ty=render_tile(wy+y);unsigned id=render_slot(tx,ty),z=town_depth[id][(y&7)*8+(x&7)];
     if(!z||ty*8+(int)z<=(d->foot8*8))pixels[death_pixel(x,y)]=0;
    }
    col+=run;p+=repeat?1:(run*bits+7)/8;
   }
  }
 }
}
static UI_FN void death_sprite_tiles(uint8_t *tiles,int wx,int wy){
 for(unsigned t=0;t<64;t++){
  uint32_t *dst=(uint32_t*)(tiles+t*64);unsigned any=0;for(unsigned p=0;p<16;p++)any|=dst[p];
  if(!any)continue;
  unsigned tx=(t&7)*8,ty=(t>>3)*8,band=lighting_tile(wx+tx,wy+ty);const uint8_t *lut=DEATH_SPRITE_LUT;
  if(band==4)lighting_pixels((uint8_t*)dst,wx+tx,wy+ty);else lut+=band*256;
  for(unsigned i=0;i<16;i++)dst[i]=lighting_word(dst[i],lut);
 }
}
/* Dungeon decoding only uses load_buffer[0..2047] and[10240..12287].
 * The bounded automap payload is confined to its first2KiB. Reuse the idle
 * town-cache / former full-map ranges for seven living-actor staging slots;
 * only slots8..12 borrow the floor/death arena. Menus restore their scratch
 * after the pending frame has committed. */
_Static_assert(18432<=sizeof(load_buffer),"living actor stage exceeds scratch");
static UI_FN uint8_t *actor_stage_buffer(unsigned slot){
 if(!slot)return DEATH_SPRITE_STAGE+10*2048;
 if(slot<=4)return (uint8_t*)load_buffer+slot*2048;
 if(slot<=7)return (uint8_t*)load_buffer+12288+(slot-5)*2048;
 return DEATH_SPRITE_STAGE+(slot-8)*2048;
}
static UI_FN int death_sprite_stage_actor(unsigned slot,const void *pixels,const uint32_t changed[2]){
 if(!DEATH_SPRITES||game.area||slot>=13)return 0;
 DeathSprites *s=DEATH_SPRITE_STATE;
 /* Every modified dungeon actor is staged. Old OAM must keep its matching
  * old pixels until the completed frame is published, including slot reuse. */
 if(!slot)FLOOR_EXTRA_KEYS[5]=65535;
 if(slot>=8&&FLOOR_EXTRA_ENABLED)FLOOR_EXTRA_KEYS[(slot-8)/2]=65535;
 unsigned tiles=0,runs=0;
 for(unsigned h=0;h<2;h++){
  unsigned bits=changed[h],starts=bits&~(bits<<1);
  while(bits){bits&=bits-1;tiles++;}while(starts){starts&=starts-1;runs++;}
 }
 /* Fragmented tiny transfers cost more than a bounded full-slot DMA. */
 D_STAGE_TILES[slot][0]=(runs>4||tiles>32)?~0u:changed[0];
 D_STAGE_TILES[slot][1]=(runs>4||tiles>32)?~0u:changed[1];
 dma(pixels,actor_stage_buffer(slot),2048);
 s->pending|=(uint16_t)(1u<<slot);return 1;
}
/* The caller admits this commit within a size-bounded VBlank window and follows it with palette
 * and OAM uploads. Keep each DMA at2KiB so pending audio IRQs can run between
 * chunks; a monolithic24KiB transfer would exceed the mixed-SFX FIFO guard. */
static UI_FN int death_sprite_upload_pending(void){
 return DEATH_SPRITES&&!game.area&&DEATH_SPRITE_STATE->pending;
}
static UI_FN unsigned death_sprite_commit_last_line(void){
 const DeathSprites *s=DEATH_SPRITE_STATE;unsigned tiles=0,runs=0;
 for(unsigned slot=0;slot<13;slot++)if(s->pending&(1u<<slot)){
  if(slot>=11){
   unsigned first=16+(slot-11)*16;
   for(unsigned i=first;i<first+16;i++)if((effect_tile_cache.pinned&(1u<<(i/4)))&&effect_tile_cache.keys[i]){tiles+=4;runs++;}
   continue;
  }
  unsigned corpse=s->count&&slot>=s->first&&slot<s->first+s->count*2;
  for(unsigned h=0;h<2;h++){
   unsigned bits=corpse?~0u:D_STAGE_TILES[slot][h],starts=bits&~(bits<<1);
   while(bits){bits&=bits-1;tiles++;}while(starts){starts&=starts-1;runs++;}
  }
 }
 /* EWRAM->VRAM payload, DMA setup and concurrent audio IRQ margin.
  * Keep at least eight spare scanlines before visible output resumes. */
 /* 32-byte EWRAM->VRAM DMA tiles cost64 cycles; this allows77 cycles
  * each,154 per run, plus eight IRQ/setup lines and eight end guard lines.
  * Atlas slots count their real128-byte dirty blocks, not an assumed2KiB. */
 unsigned lines=8+(tiles*2+31)/32+(runs+7)/8;
 return lines>=60?160u:220u-lines;
}

/* Full bounded2KiB chunks avoid fragmented-run setup in the raster transfer.
 * Buffers contain the entire final image, including transparent erasures. */
static UI_FN void death_sprite_commit_one(unsigned slot){
 DeathSprites *s=DEATH_SPRITE_STATE;
 if(slot>=11){effect_tiles_commit_range(16+(slot-11)*16,32+(slot-11)*16);s->pending&=(uint16_t)~(1u<<slot);return;}
 unsigned corpse=s->count&&slot>=s->first&&slot<s->first+s->count*2;
 const uint8_t *pixels=corpse?death_sprite_buffer(slot):actor_stage_buffer(slot);
 dma(pixels,(void*)(0x06010000+slot*2048),2048);
 s->pending&=(uint16_t)~(1u<<slot);
}
static UI_FN void death_sprite_commit_tiles(void){
 if(!DEATH_SPRITES||game.area)return;
 DeathSprites *s=DEATH_SPRITE_STATE;if(!s->pending)return;
 s->commit_start=R16(0x04000006);
 for(unsigned slot=0;slot<13;slot++)if(s->pending&(1u<<slot)){
  if(slot>=11){death_sprite_commit_one(slot);continue;}
  unsigned corpse=s->count&&slot>=s->first&&slot<s->first+s->count*2;
  const uint8_t *pixels=corpse?death_sprite_buffer(slot):actor_stage_buffer(slot);
  if(corpse)dma(pixels,(void*)(0x06010000+slot*2048),2048);
  else for(unsigned h=0;h<2;h++){
   unsigned bits=D_STAGE_TILES[slot][h];
   while(bits){
    /* D8: find the whole occupied run in constant time. Walking each bit
     * from ROM consumed VBlank time without transferring any pixels. */
    unsigned start=(unsigned)__builtin_ctz(bits),tail=bits>>start;
    unsigned end=start+(tail==~0u?32u:(unsigned)__builtin_ctz(~tail));
    dma(pixels+(h*32+start)*32,(void*)(0x06010000+slot*2048+(h*32+start)*32),(end-start)*32);
    bits=end==32?0:bits&(~0u<<end);
   }
  }
 }
}
static UI_FN void death_sprite_commit_end(void){
 if(!DEATH_SPRITES||game.area)return;
 DeathSprites *s=DEATH_SPRITE_STATE;
 if(s->pending){
  s->commit_end=R16(0x04000006);
  if(s->commit_start<160||s->commit_end<s->commit_start||s->commit_end>=228)s->overruns++;
  s->pending=0;
 }
 s->displayed=0;
 for(unsigned n=0;n<s->count;n++)s->displayed|=(uint16_t)(3u<<(s->first+n*2));
}
static UI_FN void death_sprite_draw(void){
 if(!DEATH_SPRITES||game.area)return;
 DeathSprites *s=DEATH_SPRITE_STATE;
 unsigned hardware=0;
#if LIGHT_HARDWARE
 hardware=lighting_hardware_active();
#endif
 for(unsigned n=0;n<s->count;n++){
  unsigned index=s->indices[n],slot=s->first+n*2;const Corpse *c=&game_corpses[index];
  unsigned frame=game_corpse_frame(c);
  int wx=DUNGEON_ORIGIN+((int)c->x-c->y)*16-32,wy=72+((int)c->x+c->y)*8-64;
  unsigned band=0;
#if LIGHT_HARDWARE
  if(hardware)band=lighting_at(wx+32,wy+48);
#endif
  unsigned light_key=hardware?128u|band:0;
  unsigned dirty=(s->dirty&(1u<<slot))||(!hardware&&light_moved)||light_actors[slot]!=light_key;
  if(dirty||last_asset[slot]!=asset_floor_decals||last_frame[slot]!=(int)frame||mask_x[slot]!=wx||mask_y[slot]!=wy||!death_sprite_mask_valid(slot,wx,wy)){
   uint8_t *tiles=death_sprite_buffer(slot);
   death_sprite_cached_decode(frame,tiles,slot,light_key,hardware?DEATH_SPRITE_LUT+band*256:0);
   death_sprite_mask(tiles,wx,wy,wy+64,s->floor_start[n]);
   if(!hardware)death_sprite_tiles(tiles,wx,wy);
   s->pending|=(uint16_t)(3u<<slot);
   last_asset[slot]=asset_floor_decals;last_frame[slot]=(int)frame;mask_x[slot]=wx;mask_y[slot]=wy;
   mask_cam_x[slot]=cam_x;mask_cam_y[slot]=cam_y;
   mask_complete[slot]=wx>=cam_x&&wx+64<=cam_x+240&&wy>=cam_y&&wy+64<=cam_y+160;
   light_actors[slot]=(uint8_t)light_key;
   last_asset[slot+1]=0;last_frame[slot+1]=-1;
  }
  /* All living actors/effects precede remains. Later corpse indices win. */
  add_obj(wx-cam_x,wy-cam_y,-10000+(int)index,slot*64,16,0,3);
 }
 s->dirty=0;s->cam_x=cam_x;s->cam_y=cam_y;
}
