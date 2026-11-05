/* Negative share tests must be cached per dormant actor, not per output OAM:
 * after the image budget fills, many rejected candidates use the same output
 * index and would evict one another's expensive pixel-mask verdict every draw.
 * The exact key covers tile, kind (hence native palette), direction and light
 * band. This cache is used ONLY for normal idle frame zero. Camera/terrain
 * changes invalidate entries at the viewport edge; fully tested bodies may
 * survive camera-only movement. Geometry edits invalidate all entries. A
 * stored verdict never changes source art. */
static uint32_t dormant_masks[MAX_ENEMIES],dormant_mask_epoch,dormant_mask_camera;
static UI_FN __attribute__((target("thumb"),minsize)) unsigned dormant_image_clear(unsigned slot,int wx,int wy,int foot){
 unsigned i=(unsigned)crowd_dormant_actor;const Enemy *e=&game.enemies[i];
 uint32_t key=e->x|((uint32_t)e->y<<7)|((uint32_t)e->kind<<14)|((uint32_t)game.enemy_facing[i]<<22)|((uint32_t)light_actors[slot]<<25)|0x10000000u;
 unsigned complete=wx>=cam_x&&wx+64<=cam_x+240&&wy>=cam_y&&wy+64<=cam_y+160;
 if((dormant_masks[i]&~0x60000000u)==key&&(!(dormant_masks[i]&0x40000000u)||complete))return (dormant_masks[i]>>29)&1;
 unsigned clear=crowd_image_clear_pixels(slot,wx,wy,foot);
 dormant_masks[i]=key|(clear<<29)|(complete<<30);return clear;
}
/* Dormant records remain outside simulation/collision/target selection. Their
 * presentation is static at the authoritative tile, never an old half-step.
 * Synchronize before palette planning and before any record can reactivate.
 * Only Motion is changed; no Game, AI clock or co-op checksum field is touched. */
static UI_FN __attribute__((target("thumb"),minsize)) void dormant_motion_prepare(void){
 if(game.area)return;
 uint32_t camera=(uint16_t)cam_x|((uint32_t)(uint16_t)cam_y<<16);
 if(dormant_mask_epoch!=crowd_mask_epoch||dormant_mask_camera!=camera){
  unsigned geometry_changed=dormant_mask_epoch!=crowd_mask_epoch;
  dormant_mask_epoch=crowd_mask_epoch;dormant_mask_camera=camera;
  for(unsigned i=0;i<MAX_ENEMIES;i++)if(!PERF_DORMANT_REUSE||geometry_changed||!(dormant_masks[i]&0x40000000u))dormant_masks[i]=0;
 }
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].alive==2){
  const Enemy *e=&game.enemies[i];Motion *m=&motion[i+1];
  int x=(DUNGEON_ORIGIN+(e->x-e->y)*16)*256,y=(72+(e->x+e->y)*8)*256;
  if(m->x!=x||m->y!=y||m->total)place(m,e->x,e->y);
  m->dir=game.enemy_facing[i];
 }
}
/* Active actors have already claimed their images/OAM. Fill only the spare
 * display budget, nearest first with stable record-index ties. Idle frame zero
 * is deliberately held; sleeping/stone/special poses retain their real state.
 * Nearby dormant bodies can share images, but never bypass walls/closed doors,
 * invisibility, palette differences or the sixteen-living-OBJ ceiling. */
static UI_FN __attribute__((target("thumb"),minsize)) void draw_dormant_monsters(int *sprite_slot){
 if(game.area)return;
 uint16_t candidates[MAX_ENEMIES];unsigned count=0;
 for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].alive==2){
  const Motion *m=&motion[i+1];int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;
  if(x<-32||x>272||y<0||y>208||game_enemy_hidden(&game,i))continue;
  if(!infravision_frame&&closed_door_monster_hidden(i))continue;
  int dx=(int)game.enemies[i].x-game.x,dy=(int)game.enemies[i].y-game.y;
  unsigned key=((unsigned)(dx*dx+dy*dy)<<7)|i,at=count++;
  while(at&&candidates[at-1]>key){candidates[at]=candidates[at-1];at--;}
  candidates[at]=(uint16_t)key;
 }
 for(unsigned n=0;n<count&&object_count<16;n++){
  unsigned i=candidates[n]&127;Motion *m=&motion[i+1];
  const MonsterArt *art=&monster_art[game.enemies[i].kind];
  const uint8_t *base=art->n;int frames=art->idle,anim=0;
  if(game_spell_stoned(i))monster_stone_pose(i,m,&base,&frames,&anim);
  else if(game_enemy_special(&game,i))monster_special_pose(i,m,&base,&frames,&anim);
  unsigned bank=monster_palette_bank(game.enemies[i].kind,0);
  int wx=(m->x>>8)-32,wy=(m->y>>8)-63,foot=m->y>>8;
  actor_infra_tint=infravision_frame&&(closed_door_monster_hidden(i)||lighting_at(wx+32,foot-16));
  crowd_dormant_actor=(base==art->n&&anim==0)?(int)i:-1;
  int slot=crowd_actor_frame(sprite_slot,base,m->dir,anim,frames,wx,wy,foot,&bank,1);
  crowd_dormant_actor=-1;
  actor_infra_tint=0;
  if(slot>=0)add_obj(wx-cam_x,wy-cam_y,foot,slot*64,bank,0,3);
 }
}
