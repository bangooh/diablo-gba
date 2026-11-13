/* Original MPQ panels and cursors, composed only when selection/data changes.
 * The open view borrows the actor OBJ tiles and the existing load_buffer.
 * No second world or permanent bitmap is allocated. */
#include "../generated/inventory_assets.h"
static UI_FN void inventory_controls(void);
/* The affine information backdrop uses OBJ832..839 (32x16, enlarged 2x).
 * Full canvases and the death cursor borrow it. OBJ840 onward belongs to
 * durability icons; their cache must be invalidated separately after a loan. */
static UI_FN void info_backdrop_restore(void){
 for(unsigned i=0;i<64;i++)R32(0x06010000+832*32+i*4)=0xffffffffu;
}
static unsigned inventory_previous_column=5;
static uint8_t inventory_page,inventory_cursor,inventory_slot,inventory_confirm;
static unsigned inventory_command,inventory_notice;
static unsigned inventory_art_active;
static uint16_t inventory_gold_amount;
volatile unsigned inventory_art_uploads,inventory_commit_overruns;
_Static_assert(17408+1280<=sizeof(load_buffer),"inventory scratch capacity");
static UI_FN int inventory_held(void){return game_item_drag_index(coop_active?coop_id:0);}
static UI_FN void inventory_drag(unsigned code){if(coop_active)inventory_command=game_item_drag_wire(code);else inventory_notice=game_item_drag(&game,code);}
static UI_FN __attribute__((target("thumb"),minsize)) const Inventory *inventory_view(void){return coop_active?&coop_players[coop_id].hero.inventory:&hero_inventory;}
static UI_FN int inventory_selected(void){
 if(inventory_page==3)return -1;
 if(inventory_page==2)return game_belt_kind(&game,inventory_cursor)?ITEM_SLOTS+(int)inventory_cursor:-1;
 const Inventory *v=inventory_view();
 if(inventory_page==1){for(unsigned i=0;i<ITEM_SLOTS;i++)if(v->items[i].kind<ITEM_COUNT&&v->items[i].pos==BAG_CELLS+inventory_slot)return (int)i;}
 else{uint8_t grid[40];if(items_grid(v,grid)&&grid[inventory_cursor]!=ITEM_EMPTY)return grid[inventory_cursor];}
 return -1;
}
static UI_FN uint32_t inventory_revision(void){
 const uint8_t *b=(const uint8_t*)inventory_view();uint32_t h=2166136261u;
 for(unsigned i=0;i<sizeof(Inventory);i++)h=(h^b[i])*16777619u;
 return h^(inventory_gold_amount<<4)^(uint32_t)game.belt^(uint32_t)(game.belt>>32)^(uint32_t)game.gold^((uint32_t)game_armor()<<24)^inventory_cursor^(inventory_slot<<8)^(inventory_page<<16)^(inventory_notice<<20)^(inventory_confirm<<28)^((unsigned)(inventory_held()+1)<<22);
}
/* Coordinates in the128px OBJ canvas (screen x112), derived from InvRect. */
static const uint8_t inventory_gear_rect[7][4]={
 {58,1,21,21},{26,65,11,10},{101,65,11,10},{85,12,11,10},
 {14,28,21,31},{101,28,21,31},{58,28,21,31}};
static const uint8_t inventory_bag_x[11]={14,25,36,47,57,68,79,90,101,112,123};
static const uint8_t inventory_bag_y[5]={81,92,102,113,123};
static inline __attribute__((always_inline)) unsigned inventory_pixel(unsigned x,unsigned y){
 return ((y>>6)*2+(x>>6))*4096+((y&63)>>3)*512+((x&63)>>3)*64+(y&7)*8+(x&7);
}
static UI_FN void inventory_rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color,int outline){
 uint8_t *pixels=(uint8_t*)load_buffer;
 for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++)
  if(!outline||!i||!j||i==w-1||j==h-1)pixels[inventory_pixel(x+i,y+j)]=(uint8_t)color;
}
static UI_FN void inventory_release(void){
 if(!inventory_art_active)return;
 R16(0x04000000)=0;frame_state&=~1u;
 effect_tiles_invalidate();
 apply_gamma();R16(0x0400000a)=0x1b88;
 for(unsigned i=0;i<(inventory_art_active==2?NPC_CANVAS_TILES:323);i++)terrain_invalidate(i);
 for(unsigned i=0;i<128;i++)if(i<4||i>=14){oam[i*4]=0x0200;R16(0x07000000+i*8)=0x0200;}
 ++crowd_mask_epoch;for(unsigned i=0;i<13;i++){last_frame[i]=-1;last_asset[i]=0;d_raw_base[i]=0;}
 for(unsigned i=0;i<4;i++)raw_base[i]=0;
 /* The full canvas borrows the belt, marker and level-up OBJ tiles too. */
 automap_load_markers();
 info_backdrop_restore();
 dma(levelup_tiles,(void*)(0x06010000+1000*32),sizeof(levelup_tiles));
 last_belt=last_spell_icon=~0u;durability_hud_key=~0u;globe_levels[0]=globe_levels[1]=255;
 inventory_art_active=0;map_last_x=-1;map_pending=0;last_hud_tick=~0u;
}
static UI_FN void inventory_present(void){
 lighting_hardware_stop();
 R16(0x0400000a)=0x1b80;R16(0x04000050)=0;
 for(unsigned i=0;i<128;i++)oam[i*4]=0x0200;
 vital_bars();info_box_oam();
 oam[127*4]=0x0200;oam[126*4]=0x0200;
 if(inventory_page==2){oam[14*4]=(uint16_t)((info_count?150-info_count*8:152)-3);oam[14*4+1]=(uint16_t)(80+inventory_cursor*8);oam[14*4+2]=(uint16_t)(864|(5<<12));}
 R16(0x04000050)=0x3540;R16(0x04000052)=0x0a06;
 for(unsigned i=0;i<4;i++){
  oam[(64+i)*4]=(uint16_t)((i/2)*64|0x2000);
  oam[(64+i)*4+1]=(uint16_t)((112+(i%2)*64)|0xc000);
  oam[(64+i)*4+2]=(uint16_t)(i*128|0x0400);
 }
 /* On entry, publish the complete sprite table before enabling shared OBJ
  * tiles. The previous cast/world OAM must never see inventory graphics. */
 if(!(R16(0x04000000)&0x1000u)){
  while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 }
 dma(oam,(void*)0x07000000,1024);
 R16(0x04000000)=0x3240; /* BG1+OBJ, world/map hidden. */
 if(R16(0x04000006)<160)inventory_commit_overruns++;
}
static UI_FN __attribute__((target("thumb"),minsize)) void inventory_art(void){
 const Inventory *v=inventory_view();int selected=inventory_selected();
 uint8_t *pixels=(uint8_t*)load_buffer,*icon=pixels+16384;
 unsigned cls=game_hero_class();if(cls>2)cls=0;
 lz_block(asset_inventory_panels,cls,pixels,16384);
 for(unsigned i=0;i<ITEM_SLOTS;i++){
  const Item *it=&v->items[i];if(it->kind>=ITEM_COUNT||i==(unsigned)inventory_held())continue;
  unsigned x,y,w,h;
  if(it->pos<BAG_CELLS){
   unsigned bx=it->pos%10,by=it->pos/10;
   x=inventory_bag_x[bx];y=inventory_bag_y[by];
   w=inventory_bag_x[bx+item_width(it)]-x;h=inventory_bag_y[by+item_height(it)]-y;
  }else{
   unsigned slot=it->pos-BAG_CELLS;if(slot>=EQUIP_COUNT)continue;
   const uint8_t *r=inventory_gear_rect[slot];x=r[0];y=r[1];w=r[2];h=r[3];
  }
  inventory_rect(x,y,w,h,INV_BLACK,0);
  unsigned art=item_art_index(it);lz_block(asset_inventory_icons,art,icon,1024);
  unsigned iw=inventory_icon_size[art][0],ih=inventory_icon_size[art][1];
  if(iw>w)iw=w;if(ih>h)ih=h;
  unsigned ix=x+(w-iw)/2,iy=y+(h-ih)/2;
  for(unsigned j=0;j<ih;j++)for(unsigned k=0;k<iw;k++)if(icon[j*32+k])pixels[inventory_pixel(ix+k,iy+j)]=icon[j*32+k];
 }
 /* Gold selector sits at the avatar waist, between the ring slots. */
 /* Extend the original ring-slot frame, using its own stone edge pixels. */
 for(unsigned y=0;y<12;y++)for(unsigned x=0;x<59;x++){
  unsigned sx=x<2?25+x:x>=57?36+x-57:30;
  pixels[inventory_pixel(40+x,64+y)]=pixels[inventory_pixel(sx,64+y)];
 }
 inventory_rect(42,66,55,8,INV_BLACK,0);
 /* White numeric counter; keep the native gold icon. */
 lz_decode(asset_inventory_palette,icon,1024);
 unsigned gold=INV_GOLD,best=~0u;
 for(unsigned i=1;i<256;i++){unsigned c=((uint16_t*)icon)[i];int r=(c&31)-31,g=((c>>5)&31)-31,b=((c>>10)&31)-31;unsigned d=r*r+g*g+b*b;if(d<best){best=d;gold=i;}}
 /* Use the original gold item icon in the framed waist counter. */
 lz_block(asset_inventory_icons,ITEM_GOLD,icon,1024);
 for(unsigned y=0;y<7;y++)for(unsigned x=0;x<7;x++){
  unsigned c=icon[(y*10/7)*32+x*10/7];if(c)pixels[inventory_pixel(43+x,67+y)]=(uint8_t)c;
 }
 char amount[12];amount[info_uint(amount,(unsigned)game.gold)]=0;unsigned len=0;while(amount[len])len++;
 for(unsigned i=0;i<len;i++){unsigned k=0;while(OVERLAY_GLYPHS[k]!=amount[i])k++;for(unsigned yy=0;yy<5;yy++)for(unsigned xx=0;xx<3;xx++)if(overlay_rows[k][yy]&(4>>xx))pixels[inventory_pixel(96-len*4+i*4+xx,68+yy)]=(uint8_t)gold;}
 unsigned x,y,w,h;int held=inventory_held();Item bottle={0};
 const Item *carried=held>=0?(held<ITEM_SLOTS?&v->items[held]:(bottle=item_create(game_belt_kind(&game,held-ITEM_SLOTS),1),&bottle)):0;
 if(inventory_page==3){x=40;y=64;w=59;h=12;}
 else if(inventory_page==1){const uint8_t *r=inventory_gear_rect[inventory_slot];x=r[0];y=r[1];w=r[2];h=r[3];}
 else{
  const Item *it=carried?carried:selected>=0&&selected<ITEM_SLOTS?&v->items[selected]:0;
  unsigned cell=carried?inventory_cursor:it?it->pos:inventory_cursor,bx=cell%10,by=cell/10;
  unsigned iw=it?item_width(it):1,ih=it?item_height(it):1;
  if(bx+iw>10)iw=10-bx;if(by+ih>4)ih=4-by;
  x=inventory_bag_x[bx];y=inventory_bag_y[by];w=inventory_bag_x[bx+iw]-x;h=inventory_bag_y[by+ih]-y;
 }
 if(inventory_page!=2){
  if(carried){
   unsigned art=item_art_index(carried);lz_block(asset_inventory_icons,art,icon,1024);
   unsigned iw=inventory_icon_size[art][0],ih=inventory_icon_size[art][1];if(iw>w)iw=w;if(ih>h)ih=h;
   unsigned ix=x+(w-iw)/2,iy=y+(h-ih)/2;
   for(unsigned j=0;j<ih;j++)for(unsigned k=0;k<iw;k++)if(icon[j*32+k])pixels[inventory_pixel(ix+k,iy+j)]=icon[j*32+k];
  }
  inventory_rect(x,y,w,h,inventory_confirm||inventory_notice>INV_OK?INV_RED:INV_GOLD,1);
 }
 /* Five information lines start at y120; keep the OBJ artwork from covering
  * their right half. Zero is transparent in the inventory sprite palette. */
 if(info_count==5){
  for(unsigned y=0;y<3;y++)for(unsigned x=8;x<128;x++)pixels[inventory_pixel(x,117+y)]=pixels[inventory_pixel(x,125+y)];
  for(unsigned y=120;y<128;y++)for(unsigned x=0;x<128;x++)pixels[inventory_pixel(x,y)]=0;
 }
 /* Decode in RAM first, then transfer all four sprites together in VBlank.
  * Waiting here never blanks the display or leaves partially updated tiles. */
 lz_decode(asset_inventory_palette,icon,1024);
 for(unsigned i=0;i<16;i++)((uint16_t*)icon)[80+i]=base_palette[256+80+i];
 while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
 dma(pixels,(void*)0x06010000,16384);dma(icon,(void*)0x05000200,512);
 inventory_art_active=1;inventory_art_uploads++;
}
static UI_FN __attribute__((minsize)) void inventory_draw(void){
 /* World tiles0..40 retain white details;41..81 hold selected-name color.
  * Tile82 holds white percent: tile0 must stay transparent behind artwork. */
 if(!inventory_art_active){
  lz_decode(game.area?asset_town_font:asset_dungeon_font,load_buffer,sizeof(load_buffer));outline_info_font();
  while(R16(0x04000006)>=160){}while(R16(0x04000006)<160){}
  /* Keep layers hidden until inventory_present commits fresh OAM. */
  R16(0x04000000)=0;dma(load_buffer,(void*)0x06000000,2624);
  hud_percent_tile((void*)(0x06000000+82*64),HUD_WHITE);
 }
 hud_map=(volatile uint16_t*)((uint8_t*)load_buffer+17408);
 for(unsigned i=0;i<640;i++)hud_map[i]=128;
 panel_map();
 const Inventory *v=inventory_view();int carried=inventory_held(),selected=carried>=0?carried:inventory_selected();
 const Item *it=selected>=0&&selected<ITEM_SLOTS?&v->items[selected]:0;
 for(unsigned y=0;y<16;y++)hud_text(0,(int)y,"                              ");
 info_reset();if(it)info_item(it,1);else if(carried>=ITEM_SLOTS||inventory_page==2){unsigned kind=game_belt_kind(&game,carried>=ITEM_SLOTS?(unsigned)carried-ITEM_SLOTS:inventory_cursor);if(kind){Item bottle={.kind=(uint8_t)kind};info_item(&bottle,1);}}info_render();
 for(unsigned i=0;i<640;i++)hud_map[i]+=512;
 /* Leave the artwork area transparent; BG1 information stays above OBJ. */
 for(unsigned y=0;y<16;y++)for(unsigned x=15;x<30;x++)hud_map[y*32+x]=0;
 for(unsigned y=20-info_count;y<20;y++)for(unsigned x=4;x<26;x++)hud_map[y*32+x]=0;
 for(unsigned row=0;row<info_count;row++){
  unsigned len=0;while(info_lines[row][len])len++;unsigned x=4+(22-len)/2;
  unsigned font=info_line_color(row)==HUD_WHITE?1:42;
  for(unsigned i=0;i<len;i++){unsigned k=hud_glyph_index[(uint8_t)info_lines[row][i]];k=k?k-1:36;hud_map[(20-info_count+row)*32+x+i]=(uint16_t)(k==40?(font==1?82:41):k+font);}
 }
 if(info_name_style>>3)hud_font_color_copy((void*)(0x06000000+41*64),info_name_style>>3);
 inventory_art(); /* Decodes without touching staging map; uploads in VBlank. */
 inventory_controls();
 dma((const void*)hud_map,(void*)0x0600d800,1280);
 hud_map=(volatile uint16_t*)0x0600d800;

}
/* DevilutionX InventoryMove: empty-handed selection skips item footprints;
 * held-item placement moves one cell at a time, including exact belt slots. */
static UI_FN void inventory_move(int dx,int dy){
 if(inventory_page==3){
  if(dy>0){inventory_page=0;inventory_cursor=5;}
  else if(dy<0){inventory_page=1;inventory_slot=EQUIP_CHEST;}
  else if(dx){inventory_page=1;inventory_slot=dx<0?EQUIP_RING1:EQUIP_RING2;}
  return;
 }
 if(inventory_page==2){
  if(dx<0&&inventory_cursor)inventory_cursor--;
  if(dx>0&&inventory_cursor<9)inventory_cursor++;
  if(dy<0){inventory_page=0;inventory_cursor+=30;}
  return;
 }
 if(inventory_page==1){
  unsigned slot=inventory_slot;
  if(dx<0){
   if(slot==EQUIP_SHIELD)slot=EQUIP_CHEST;
   else if(slot==EQUIP_CHEST)slot=EQUIP_HAND;
   else if(slot==EQUIP_NECK)slot=EQUIP_HEAD;
   else if(slot==EQUIP_RING2){if(inventory_held()<0)inventory_page=3;else slot=EQUIP_RING1;}
  }else if(dx>0){
   if(slot==EQUIP_HAND)slot=EQUIP_CHEST;
   else if(slot==EQUIP_CHEST)slot=EQUIP_SHIELD;
   else if(slot==EQUIP_HEAD)slot=EQUIP_NECK;
   else if(slot==EQUIP_RING1){if(inventory_held()<0)inventory_page=3;else slot=EQUIP_RING2;}
  }
  if(dy<0){
   if(slot==EQUIP_CHEST||slot==EQUIP_HAND)slot=EQUIP_HEAD;
   else if(slot==EQUIP_RING1)slot=EQUIP_HAND;
   else if(slot==EQUIP_RING2)slot=EQUIP_SHIELD;
   else if(slot==EQUIP_SHIELD)slot=EQUIP_NECK;
  }else if(dy>0){
   if(slot==EQUIP_CHEST&&inventory_held()<0){inventory_page=3;return;}
   if(slot==EQUIP_HEAD)slot=EQUIP_CHEST;
   else if(slot==EQUIP_HAND)slot=EQUIP_RING1;
   else if(slot==EQUIP_SHIELD)slot=EQUIP_RING2;
   else if(slot==EQUIP_NECK)slot=EQUIP_SHIELD;
   else{
    unsigned lo=slot==EQUIP_RING1?0:slot==EQUIP_RING2?7:3;
    unsigned hi=slot==EQUIP_RING1?2:slot==EQUIP_RING2?9:6;
    inventory_cursor=inventory_previous_column>=lo&&inventory_previous_column<=hi?inventory_previous_column:slot==EQUIP_RING1?1:slot==EQUIP_RING2?8:5;
    inventory_page=0;
   }
  }
  inventory_slot=slot;return;
 }
 uint8_t grid[BAG_CELLS];if(!items_grid(inventory_view(),grid))return;
 int x=inventory_cursor%10,y=inventory_cursor/10;unsigned item=inventory_held()>=0?ITEM_EMPTY:grid[inventory_cursor];
 for(unsigned n=0;n<10;n++){
  x+=dx;y+=dy;
  if(x<0||x>=10)return;
  if(y>=4){inventory_page=2;inventory_cursor=(unsigned)x;return;}
  if(y<0){inventory_previous_column=(unsigned)x;inventory_page=1;inventory_slot=x<3?EQUIP_RING1:x<7?EQUIP_CHEST:EQUIP_RING2;if(x>=3&&x<7&&inventory_held()<0)inventory_page=3;return;}
  unsigned cell=(unsigned)(y*10+x);
  if(item==ITEM_EMPTY||grid[cell]!=item){inventory_cursor=cell;return;}
 }
}
static UI_FN void inventory_input(unsigned held_keys,unsigned pressed){
 /* Identify/repair/recharge use a target cursor. Cancel consumes nothing. */
 if(inventory_confirm==4||inventory_confirm==5){
  if(pressed&KEY_B){inventory_confirm=0;inventory_notice=0;return;}
  if(pressed&KEY_LEFT)inventory_move(-1,0);else if(pressed&KEY_RIGHT)inventory_move(1,0);
  if(pressed&KEY_UP)inventory_move(0,-1);else if(pressed&KEY_DOWN)inventory_move(0,1);
  if(pressed&KEY_A){int i=inventory_selected();unsigned index=i<0?ITEM_SLOTS:(unsigned)i,skill=inventory_confirm==5;
   if(coop_active){inventory_command=ITEM_COMMAND_MAKE(index,skill)|ITEM_IDENTIFY_COMMAND;inventory_confirm=0;}
   else{inventory_notice=skill?game_skill_item(&game,index):game_identify_item(&game,index);if(inventory_notice==INV_OK)inventory_confirm=0;}
  }return;
 }
 if(inventory_confirm==3){
  unsigned max=(unsigned)game.gold/100*100;if(max>5000)max=5000;
  if(pressed&KEY_B){inventory_confirm=0;return;}
  if(!max){inventory_confirm=0;return;}
  if(inventory_gold_amount>max)inventory_gold_amount=max;
  if((pressed&(KEY_UP|KEY_RIGHT))&&inventory_gold_amount<max)inventory_gold_amount+=100;
  else if((pressed&(KEY_DOWN|KEY_LEFT))&&inventory_gold_amount>100)inventory_gold_amount-=100;
  if(pressed&KEY_A){if(coop_active)inventory_command=GOLD_COMMAND_MAKE(inventory_gold_amount/100);else inventory_notice=game_gold_drop(&game,inventory_gold_amount);inventory_confirm=0;inventory_page=0;}
  return;
 }
 if(inventory_page==3&&(pressed&KEY_A)&&inventory_held()<0){if(game.gold>=100){inventory_confirm=3;inventory_gold_amount=100;inventory_notice=0;}return;}
 if(inventory_held()>=0){
  if(pressed&KEY_B){inventory_drag(127);return;}
  if(pressed&KEY_LEFT)inventory_move(-1,0);else if(pressed&KEY_RIGHT)inventory_move(1,0);
  if(pressed&KEY_UP)inventory_move(0,-1);else if(pressed&KEY_DOWN)inventory_move(0,1);
  if(pressed&KEY_A)inventory_drag(64+(inventory_page==2?BAG_CELLS+EQUIP_COUNT+inventory_cursor:inventory_page==1?BAG_CELLS+inventory_slot:inventory_cursor));
  return;
 }

 if(inventory_confirm){
  if(pressed&KEY_B){inventory_confirm=0;return;}
  if(!(pressed&KEY_A))return;
 }else{
  if((pressed&KEY_R)&&(held_keys&KEY_SELECT)){inventory_confirm=5;return;}
  if(pressed&KEY_LEFT)inventory_move(-1,0);
  else if(pressed&KEY_RIGHT)inventory_move(1,0);
  if(pressed&KEY_UP)inventory_move(0,-1);
  else if(pressed&KEY_DOWN)inventory_move(0,1);
 }
 int i=inventory_selected();
 if(inventory_confirm==2){
  if(coop_active)inventory_command=ITEM_COMMAND_MAKE(i<0?ITEM_SLOTS:(unsigned)i,1)|ITEM_IDENTIFY_COMMAND;
  else inventory_notice=game_skill_item(&game,i<0?ITEM_SLOTS:(unsigned)i);
  inventory_confirm=0;return;
 }
 if(i<0)return;
 if(!inventory_confirm&&(pressed&KEY_SELECT)){
  if(coop_active)inventory_command=ITEM_COMMAND_MAKE((unsigned)i,0)|ITEM_IDENTIFY_COMMAND;
  else inventory_notice=game_identify_item(&game,(unsigned)i);return;
 }
 if(!inventory_confirm&&(pressed&KEY_L)){inventory_confirm=1;return;}
 if(!inventory_confirm&&(pressed&KEY_R)){inventory_confirm=6;return;}
 if(!inventory_confirm&&(pressed&KEY_A)){inventory_drag((unsigned)i);return;}
 if(!(pressed&(inventory_confirm?KEY_A:KEY_R)))return;
 if(inventory_confirm==6&&i<ITEM_SLOTS&&inventory_view()->items[i].kind==ITEM_SCROLL_IDENTIFY){inventory_confirm=4;inventory_notice=0;return;}
 unsigned drop=inventory_confirm==1;
 if(coop_active)inventory_command=ITEM_COMMAND_MAKE((unsigned)i,drop);
 else inventory_notice=game_item_action(&game,(unsigned)i,(int)drop);
 inventory_confirm=0;
}
