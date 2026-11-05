/* Original CEL icons at native16px (no affine enlargement).
 * OBJ840..863: three8bpp icons. Fourth:958..961,866..867,998..999.
 * Panel832..839; actor/effect/stair/belt/map/skill tiles remain disjoint.
 * OAM14..19; colors use transparent entry zero of4bpp banks1..15. */
#define DURABILITY_FN static UI_FN __attribute__((target("thumb"),minsize))
#include "durability_hud.h"
#undef DURABILITY_FN
#include "../generated/native_durability.h"
static uint32_t durability_hud_key=~0u;
static UI_FN __attribute__((target("thumb"),minsize)) void durability_hud_palette(void){
 for(unsigned i=1;i<16;i++)base_palette[256+i*16]=durability_native_colors[i-1];
}
static UI_FN __attribute__((target("thumb"),minsize)) void durability_hud_upload(unsigned index,unsigned code){
 uint32_t pixels[64];uint8_t *bytes=(uint8_t*)pixels;
 unsigned frame=(code&15)-1+((code&DURABILITY_YELLOW)?8:0);
 lz_block(asset_native_durability,frame,pixels,128);
 for(int i=255;i>=0;i--)bytes[i]=(uint8_t)(((bytes[i/2]>>((i&1)*4))&15)<<4);
 if(index<3)dma(pixels,(void*)(0x06010000u+(840+index*8)*32u),256);
 else{
  dma(pixels,(void*)(0x06010000u+958*32u),128);
  dma(bytes+128,(void*)(0x06010000u+866*32u),64);
  dma(bytes+192,(void*)(0x06010000u+998*32u),64);
 }
}
static UI_FN __attribute__((target("thumb"),minsize)) void durability_hud(void){
 const Inventory *bag=coop_active?&coop_players[coop_id].hero.inventory:&hero_inventory;
 uint8_t icons[DURABILITY_WARNINGS];unsigned count=durability_hud_collect(bag,icons);
 uint32_t key=0;
 for(unsigned n=0;n<DURABILITY_WARNINGS;n++)key|=(uint32_t)icons[n]<<(n*8);
 if(key!=durability_hud_key){
  for(unsigned n=0;n<count;n++)durability_hud_upload(n,icons[n]);
  durability_hud_key=key;
 }
 for(unsigned n=0;n<count;n++){
  unsigned at=(14+n)*4;
  oam[at]=4|0x2000;oam[at+1]=(uint16_t)((220-n*18)|0x4000);
  oam[at+2]=(uint16_t)(840+n*8);
 }
 if(count==4){
  oam[17*4]=4|0x6000;oam[17*4+1]=166;oam[17*4+2]=958;
  oam[18*4]=12|0x2000;oam[18*4+1]=166;oam[18*4+2]=866;
  oam[19*4]=12|0x2000;oam[19*4+1]=174;oam[19*4+2]=998;
 }
}
