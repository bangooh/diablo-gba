/* Hardware actor shading uses only banks absent from this draw's ordinary
 * 4bpp OAM users. 8bpp deaths force the immutable full palette, exactly as
 * their scene LUT expects. Palette/OAM commits must share a safe VBlank. */
#include "actor_palette.h"
static ActorPaletteState actor_palettes;
volatile unsigned actor_palette_hw_actors,actor_palette_cpu_actors;
volatile unsigned actor_palette_bank_uploads;
static UI_FN void actor_palette_reset(void){
 actor_palette_state_reset(&actor_palettes);
 actor_palette_hw_actors=actor_palette_cpu_actors=0;
}
/* apply_gamma first restores all ordinary banks. Current assignments only
 * refer to ordinary source banks, so reading hardware preserves exact gamma
 * rounding without another color workspace or per-actor RGB calculation. */
static UI_FN void actor_palette_refresh(void){
 for(unsigned b=0;b<16;b++)if(actor_palettes.current[b]>=16){
  unsigned source=actor_palettes.current[b]-16;
  for(unsigned i=1;i<16;i++)R16(0x05000200+b*32+i*2)=R16(0x05000200+source*32+light_obj[source*16+i]*2);
 }
}
static UI_FN void actor_palette_plan(void){
 unsigned reserved=(1u<<3)|(1u<<5)|(1u<<15),dark=0;
 actor_palette_hw_actors=actor_palette_cpu_actors=0;
 if(coop_active||game.area||(DEATH_SPRITES&&DEATH_SPRITE_STATE->count)){
  actor_palette_state_plan(&actor_palettes,65535,0);return;
 }
 /* Protect all co-op class banks even if a remote hero is offscreen. This
  * also preserves the scene's class-palette sharing when members reappear. */
 if(coop_active)reserved|=(1u<<0)|(1u<<4)|(1u<<6);
 unsigned infravision=infravision_frame;
 int target=game.attack_wait?game.swing_target:game.target;
 for(unsigned i=0;i<=MAX_ENEMIES;i++){
  if(i&&(!game.enemies[i-1].alive||game_enemy_hidden(&game,i-1)))continue;
  const Motion *m=&motion[i];int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;
  if(x<-32||x>272||y<0||y>208)continue;
  unsigned bank=i?monster_palette_bank(game.enemies[i-1].kind,!coop_active&&(int)i-1==target):hero_palette_bank(hero_render_variants[coop_active?coop_id:0]==27?3:game_hero_class());
  reserved|=1u<<bank;
  if(!(i&&infravision)&&lighting_at(m->x>>8,(m->y>>8)-16))dark|=1u<<bank;
 }
 if(coop_active)for(unsigned i=0;i<4;i++)if(i!=coop_id&&(coop_members&(1u<<i))){
  const Motion *m=&coop_motion[i];int x=(m->x>>8)-cam_x,y=(m->y>>8)-cam_y;
  if(x<-32||x>272||y<0||y>208)continue;
  unsigned cls=coop_players[i].hero.reserved&255;if(cls>2)cls=0;
  unsigned bank=hero_palette_bank(hero_render_variants[i]==27?3:cls);reserved|=1u<<bank;
  if(!infravision&&lighting_at(m->x>>8,(m->y>>8)-16))dark|=1u<<bank;
 }
 actor_palette_state_plan(&actor_palettes,reserved,dark);
}
/* Return the OAM bank and only the residual software band. Raw tile caches
 * then survive transitions between bright and hardware-dark actor palettes. */
static UI_FN unsigned actor_palette_resolve(unsigned bank,unsigned *band){
 if(*band){
  unsigned display=actor_palette_state_bank(&actor_palettes,bank);
  if(display!=bank){*band=0;actor_palette_hw_actors++;return display;}
  actor_palette_cpu_actors++;
 }
 return bank;
}
static UI_FN int actor_palette_pending(void){return actor_palette_state_pending(&actor_palettes);}
/* Caller waits for a safe early VBlank only when pending() is true, then
 * calls this immediately before uploading the matching OAM. Restore normal
 * source banks first; they may have been dark duplicates in the last draw. */
static UI_FN void actor_palette_commit(void){
 for(unsigned b=0;b<16;b++)if(actor_palettes.current[b]!=actor_palettes.next[b]&&actor_palettes.current[b]>=16){
  for(unsigned i=1;i<16;i++)R16(0x05000200+b*32+i*2)=actor_palette_gamma(base_palette[256+b*16+i],gamma_level);
  actor_palette_bank_uploads++;
 }
 for(unsigned b=0;b<16;b++)if(actor_palettes.current[b]!=actor_palettes.next[b]){
  unsigned key=actor_palettes.next[b];
  if(key>=16){
   unsigned source=key-16;
   for(unsigned i=1;i<16;i++)R16(0x05000200+b*32+i*2)=R16(0x05000200+source*32+light_obj[source*16+i]*2);
   actor_palette_bank_uploads++;
  }
  actor_palettes.current[b]=(uint8_t)key;
 }
}
