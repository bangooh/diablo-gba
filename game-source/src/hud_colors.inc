/* Devilution PrintChar remaps the font's brightest gray240 to blue178,
 * yellow194 or red224. These RGB5 targets come from the original Town.PAL.
 * Use the nearest immutable scene color; never modify terrain palette slots
 * or select a dungeon water-animation entry. Gamma remains a display step. */
enum { HUD_WHITE=0, HUD_BLUE=1, HUD_GOLD=2, HUD_RED=3, HUD_MENU=4 };
static UI_FN unsigned hud_color_index(unsigned color){
 static const uint16_t targets[5]={
  30|(30<<5)|(30<<10),19|(20<<5)|(24<<10),
  27|(24<<5)|(15<<10),31|(23<<5)|(23<<10),
  27|(22<<5)|(13<<10) /* frontend palette 253 */
 };
 unsigned target=targets[color<5?color:HUD_WHITE],best=~0u,index=1;
 for(unsigned i=1;i<256;i++){
  if(!game.area&&!bg_static_index(i))continue;
  unsigned value=base_palette[i];
  int r=(int)(value&31)-(int)(target&31);
  int g=(int)((value>>5)&31)-(int)((target>>5)&31);
  int b=(int)((value>>10)&31)-(int)((target>>10)&31);
  unsigned distance=(unsigned)(r*r+g*g+b*b);
  if(distance<best){best=distance;index=i;}
 }
 return index;
}
