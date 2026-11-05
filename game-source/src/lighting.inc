#include "light_math.h"
#ifndef SPELL_LIGHTING
#define SPELL_LIGHTING 0
#endif
/* Presentation only: not serialized or included in the deterministic game hash.
 * BG terrain uses8bpp, actors use16-color banks. Keep raw/depth caches unshaded. */
static uint8_t light_bg[256],light_obj[256];
static uint8_t light_tiles[WORLD_TILES],light_actors[13],light_banks[13];
static LightSource light_sources[LIGHT_CAPACITY];
static unsigned light_count;
static unsigned light_moved;
static UI_FN void lighting_source(int x,int y,unsigned radius,unsigned flat){
 if(light_count>=LIGHT_CAPACITY)return;
 unsigned i=light_count++;
 if(light_sources[i].x!=x||light_sources[i].y!=y||light_sources[i].radius!=radius||light_sources[i].flat!=flat)light_moved=1;
 light_sources[i]=(LightSource){x,y,radius,flat};
}
static UI_FN void lighting_palette(void){
 for(unsigned i=0;i<256;i++){
  light_bg[i]=light_color_masked(base_palette,256,i,3,game.area?0:bg_static_slots);
  unsigned bank=i/16;light_obj[i]=light_color(base_palette+256+bank*16,16,i%16,3);
 }
}
static UI_FN void lighting_sources(void){
 unsigned previous=light_count;light_count=0;light_moved=0;if(game.area||coop_active){light_moved=previous!=0;return;}
 lighting_source(motion[0].x>>8,(motion[0].y>>8)-16,game_light_radius(),1);
 if(coop_active)for(unsigned i=0;i<4;i++)if(i!=coop_id&&(coop_members&(1u<<i))&&coop_players[i].hero.hp>0){
  const CharacterState *h=&coop_players[i].hero;ItemEffects effects;
  item_build_effects(&h->progress,&h->inventory,h->reserved&255,&effects);
  lighting_source(coop_motion[i].x>>8,(coop_motion[i].y>>8)-16,effects.light_radius,1);
 }
 if(dungeon_level==16&&quest_progress.state[5]==3&&light_count<5)for(unsigned i=0;i<MAX_ENEMIES;i++)if(game.enemies[i].kind==MONSTER_DIABLO){
  const Enemy *e=&game.enemies[i];lighting_source(DUNGEON_ORIGIN+(e->x-e->y)*16,72+(e->x+e->y)*8-16,8,1);break;
 }
 /* OPT119: prop flames remain animated but emit no local light. */
#if SPELL_LIGHTING
 /* Reserve up to four visible persistent/wave lights inside the existing
  * thirteen-light budget. Rendering and damage have independent capacities. */
 unsigned extra_lights=0;
 if(game_spell_effects_active())for(unsigned i=0;i<SPELL_EFFECT_VIEWS&&extra_lights<4;i++){
  int wx,wy;unsigned spell=game_spell_effect_view(i,&wx,&wy);
  if(spell!=SPELL_FIREWALL&&spell!=SPELL_GUARDIAN&&spell!=SPELL_NOVA)continue;
  if(spell==SPELL_NOVA&&(i&1))continue;
  if(wx<cam_x-32||wx>cam_x+272||wy<cam_y-32||wy>cam_y+192)continue;
  lighting_source(wx,wy-8,3,1);extra_lights++;
 }
 /* Spell casts share a small light budget, independently of their lane count.
  * First/last visible lanes represent a spread. Give every cast its first
  * source before admitting a second; total sources still obey LIGHT_CAPACITY. */
 if(game_spell_bolts_active()){
  int16_t lx[MAX_SPELL_CASTS][2],ly[MAX_SPELL_CASTS][2];uint8_t seen[MAX_SPELL_CASTS]={0};
  for(unsigned i=0;i<MAX_SPELL_BOLTS;i++){
   int wx,wy;unsigned spell=game_spell_bolt_world(i,projectile_phase,&wx,&wy);
   if(!spell||wx<cam_x-32||wx>cam_x+272||wy<cam_y-32||wy>cam_y+192)continue;
   unsigned group=game_spell_bolts[i].cast-1,slot=seen[group]?1:0;
   lx[group][slot]=wx;ly[group][slot]=wy-(spell==SPELL_INFERNO?8:24);seen[group]=slot+1;
  }
  unsigned emitted=0;
  for(unsigned pass=0;pass<2;pass++)for(unsigned group=0;group<MAX_SPELL_CASTS&&emitted<8&&light_count<LIGHT_CAPACITY;group++)if(seen[group]>pass){
   if(pass&&lx[group][0]==lx[group][1]&&ly[group][0]==ly[group][1])continue;
   lighting_source(lx[group][pass],ly[group][pass],3,1);emitted++;
  }
 }
 /* Match draw_spell_effects' world position, including monster/fire arrows.
  * Fixed radius/brightness; neither animation phase nor age changes the light. */
 for(unsigned i=0;i<MAX_ARROWS;i++)if(game_arrows[i].life){
  const Arrow *a=&game_arrows[i];
  if(a->owner==HOSTILE_TRAP_OWNER&&a->reserved==GAME_TRAP_LIGHTNING&&!(a->direction&HOSTILE_BURST))continue;
  unsigned spell=(a->direction&64)?a->fire:game_spell_missile(i);
  if(!spell&&(a->fire||(a->direction&(HOSTILE_PROJECTILE|HOSTILE_FARROW))==(HOSTILE_PROJECTILE|HOSTILE_FARROW)))spell=SPELL_FIREBOLT;
  if(!spell&&!(a->direction&(HOSTILE_PROJECTILE|128))&&a->lightning)spell=SPELL_LIGHTNING;
  if(spell!=SPELL_FIREBOLT&&spell!=SPELL_FIREBALL&&spell!=SPELL_FIREWALL&&spell!=SPELL_INFERNO&&spell!=SPELL_GUARDIAN&&spell!=SPELL_WAVE&&spell!=SPELL_ELEMENTAL&&spell!=SPELL_LIGHTNING&&spell!=SPELL_CHAIN&&spell!=SPELL_CHARGED&&spell!=SPELL_NOVA&&spell!=SPELL_HOLY&&spell!=SPELL_BLOOD_STAR&&spell!=SPELL_BONE_SPIRIT&&spell!=SPELL_FLASH&&spell!=SPELL_APOCALYPSE)continue;
  int ground=spell==SPELL_FIREWALL||spell==SPELL_INFERNO||spell==SPELL_GUARDIAN||spell==SPELL_FLASH||spell==SPELL_APOCALYPSE||(a->direction&(HOSTILE_PROJECTILE|HOSTILE_BURST))==(HOSTILE_PROJECTILE|HOSTILE_BURST);
  int wx,wy;projectile_world(a,spell,&wx,&wy);
  lighting_source(wx,wy-(ground?8:24),3,1);
 }
#endif
 if(previous!=light_count)light_moved=1;
}
static UI_FN unsigned lighting_at(int x,int y){
 if(!light_count)return 0;unsigned best=3;
 for(unsigned i=0;i<light_count;i++){
  int dx=x-light_sources[i].x,dy=y-light_sources[i].y;
  unsigned b=light_band(dx,dy,light_sources[i].radius);
  if(b<best)best=b;
  if(!best)break;
 }
 return best;
}
static inline uint32_t lighting_word(uint32_t v,const uint8_t *lut){
 return lut[v&255]|(lut[(v>>8)&255]<<8)|(lut[(v>>16)&255]<<16)|((uint32_t)lut[v>>24]<<24);
}
static __attribute__((noinline)) void lighting_terrain(uint8_t *pixels,unsigned band){
 if(band){const uint8_t *lut=light_bg;uint32_t *words=(uint32_t*)pixels;
  for(unsigned p=0;p<16;p++)words[p]=lighting_word(words[p],lut);
 }
}
/* Binary lighting has one80:50 ellipse boundary. Software terrain routines
 * remain available for builds explicitly compiled with LIGHT_HARDWARE=0;
 * the default dungeon renderer never selects them for excess light spans. */
#include "../generated/light_spans.h"
/* Hardware terrain lighting bypasses this software-only classification. */
static UI_FN unsigned lighting_tile(int x,int y){
 unsigned lo=3,hi=3;if(!light_count)return 0;
 for(unsigned i=0;i<light_count;i++){
  int fx=x-light_sources[i].x,fy=y-light_sources[i].y;
  fx=fx<-3?-fx:fx+7;fy=fy<-3?-fy:fy+7;
  int nx=fx>7?fx-7:0,ny=fy>7?fy-7:0;
  unsigned radius=light_sources[i].radius;
  if(radius>15||nx>(int)radius*8||ny>(int)radius*5)continue;
  unsigned near=(unsigned)(25*nx*nx+64*ny*ny),far=(unsigned)(25*fx*fx+64*fy*fy);
  unsigned r=1600*radius*radius;
  unsigned a=near>r?3:0,b=far>r?3:0;
  if(a<lo)lo=a;if(b<hi)hi=b;
  if(!hi)return 0;
 }
 return lo==hi?lo:4;
}
static UI_FN void lighting_pixels(uint8_t *pixels,int x,int y){
 if(!light_count)return;
 for(unsigned row=0;row<8;row++){
  unsigned dark=255;
  for(unsigned i=0;i<light_count&&dark;i++){
   const LightSource *s=&light_sources[i];int dy=y+(int)row-s->y;if(dy<0)dy=-dy;if(dy>75||s->radius>15)continue;
   unsigned span=light_spans[s->radius][dy];if(span==255)continue;
   int left=s->x-(int)span-x,right=s->x+(int)span-x;
   if(left<0)left=0;if(right>7)right=7;if(left>right)continue;
   dark&=~(((1u<<(right-left+1))-1)<<left);
  }
  if(!dark)continue;
  if(dark==255){uint32_t *p=(uint32_t*)(pixels+row*8);p[0]=lighting_word(p[0],light_bg);p[1]=lighting_word(p[1],light_bg);continue;}
  for(unsigned col=0;col<8;col++,dark>>=1)if(dark&1)pixels[row*8+col]=light_bg[pixels[row*8+col]];
 }
}
static UI_FN void lighting_actor(uint8_t *pixels,unsigned band,unsigned bank,const uint32_t tiles[2]){
 if(band&&(tiles[0]|tiles[1])){const uint8_t *lut=light_obj+bank*16;
  /* GBA adaptation: immutable UI bank5 supplies three red heat shades.
   * No palette writes: safe with 8bpp corpses, multiplayer and spell banks.
   * This approximates Infra.TRN; it is not the original 256-color mapping. */
  uint8_t heat[16];
  if(band==2){heat[0]=0;for(unsigned i=1;i<16;i++){
   unsigned c=base_palette[256+bank*16+i];
   unsigned l=(c&31)*3+((c>>5)&31)*6+((c>>10)&31);
   heat[i]=l<80?4:l<170?5:6;
  }lut=heat;}
  /* Expand the 16-color map on the stack: one lookup per packed byte and
   * word transfers replace two nibble lookups and byte transfers per pixel
   * pair. No actor cache or persistent palette workspace is added. */
  uint32_t packed[64],low[4];
  for(unsigned i=0;i<4;i++)low[i]=lut[i*4]|(lut[i*4+1]<<8)|(lut[i*4+2]<<16)|((uint32_t)lut[i*4+3]<<24);
  for(unsigned hi=0;hi<16;hi++){
   unsigned high=lut[hi]*0x10101010u;
   for(unsigned lo=0;lo<4;lo++)packed[hi*4+lo]=low[lo]|high;
  }
  uint32_t *words=(uint32_t*)pixels;const uint8_t *bytes=(const uint8_t*)packed;
  for(unsigned half=0;half<2;half++){
   unsigned bits=tiles[half];
   for(unsigned tile=0;tile<32;tile++,bits>>=1,words+=8)if(bits&1)
    for(unsigned p=0;p<8;p++)words[p]=lighting_word(words[p],bytes);
  }
 }
}
