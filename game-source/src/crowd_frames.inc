/* The rectangle can overlap a wall only in transparent padding. Test the
 * final source pixels using the same production occlusion predicate before
 * accepting such a share. Never modify the source VRAM or its cached image. */
static UI_FN unsigned crowd_image_clear_pixels(unsigned slot,int wx,int wy,int foot){
 if(actor_clear_geometry(wx,wy,foot))return 1;
 const volatile uint32_t *source=(const volatile uint32_t*)(0x06010000+slot*2048);
 /* Living images are staged until the next VBlank. Inspect the pending
  * pixels, not the previous frame/corpse still resident in VRAM. */
 if(DEATH_SPRITES&&(DEATH_SPRITE_STATE->pending&(1u<<slot)))source=(const uint32_t*)actor_stage_buffer(slot);
 for(unsigned tile=0;tile<64;tile++){
  uint32_t words[8],any=0;for(unsigned row=0;row<8;row++){words[row]=source[tile*8+row];any|=words[row];}
  if(!any)continue;
  unsigned hidden=0;
  actor_mask_tile(words,wx+(tile&7)*8,wy+(tile>>3)*8,foot,0,&hidden);
  if(hidden)return 0;
 }
 return 1;
}
/* At most sixteen living OAM entries are emitted. Cache both positive and
 * negative destination tests; sleeping/idle crowds must not repeat pixel-mask
 * work every draw. Terrain-opacity edits, scene/UI rebuilds and revival bump
 * the epoch. Camera, pose, location and lighting are explicit keys. */
typedef struct {
 const uint8_t *asset;uint32_t epoch;
 int16_t x,y,foot,cam_x,cam_y;
 uint8_t dir,frame,bank,band,result,complete;
} CrowdMask;
static CrowdMask crowd_masks[16];
static UI_FN unsigned crowd_image_clear(unsigned slot,int wx,int wy,int foot){
 CrowdMask *c=&crowd_masks[object_count];
 if(c->result&&c->epoch==crowd_mask_epoch&&c->asset==last_asset[slot]&&
    c->dir==last_dir[slot]&&c->frame==last_frame[slot]&&c->bank==light_banks[slot]&&c->band==light_actors[slot]&&
    c->x==wx&&c->y==wy&&c->foot==foot&&(c->complete||(c->cam_x==cam_x&&c->cam_y==cam_y)))return c->result-1;
 unsigned clear=crowd_image_clear_pixels(slot,wx,wy,foot);
 *c=(CrowdMask){last_asset[slot],crowd_mask_epoch,wx,wy,foot,cam_x,cam_y,last_dir[slot],last_frame[slot],light_banks[slot],light_actors[slot],clear+1,wx>=cam_x&&wx+64<=cam_x+240&&wy>=cam_y&&wy+64<=cam_y+160};
 return clear;
}
/* Current-frame actors can share an unchanged, unmasked OBJ image. A second
 * world position still owns independent OAM/depth ordering, but needs no extra
 * 2 KiB image slot. Match the post-resolution light band as well as the source
 * palette: software-shaded pixels must never leak between lighting variants.
 * This does not stop monster simulation, attacks or special animations. */
#ifndef CROWD_IMAGE_CLEAR
#define CROWD_IMAGE_CLEAR(slot,wx,wy,foot) crowd_image_clear(slot,wx,wy,foot)
#endif
static UI_FN int crowd_actor_frame(int *next,const uint8_t *base,int dir,int frame,int count,int wx,int wy,int foot,unsigned *bank,int monster){
 if(monster&&!game.area){
  unsigned band=lighting_at(wx+32,foot-16);
  if(infravision_frame)band=actor_infra_tint?2:0;
  unsigned display=band==2?5:actor_palette_resolve(*bank,&band);
  for(int slot=1;slot<*next;slot++){
   if(last_asset[slot]!=base||last_dir[slot]!=dir||last_frame[slot]!=frame||
      light_actors[slot]!=band||light_banks[slot]!=*bank||
      !(actor_unmasked_slots&(1u<<slot))||(actor_hidden_slots&(1u<<slot)))continue;
   if(CROWD_IMAGE_CLEAR(slot,wx,wy,foot)){*bank=display;return slot;}
   break;
  }
 }
 if(*next>=(game.area?13:11))return -1;
 int slot=*next;
 *bank=load_town_frame(slot,base,dir,frame,count,wx,wy,foot,*bank);
 if(monster&&(actor_hidden_slots&(1u<<slot)))return -1;
 ++*next;return slot;
}
