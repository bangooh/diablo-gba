/* Include after dma(). Reload only after radius changes or scene/font reload.
 * BG3 priority1 sits above world BG0/OBJ2, below HUD BG1/OBJ0 and automap
 * BG2 priority1 (the lower BG number wins). Info OBJ1 also remains above it.
 *
 * Gameplay reuses title-font cells: menus must disable BG3 before restoring
 * large glyphs; gameplay must reload this mask and invalidate the minimap
 * after restoring transparent fonts. Do not enable during finale camera pan:
 * its scrolling camera center differs from this normal (120,68) light center.
 */
#include "../generated/outer_mask.h"
#define OUTER_MAP_CHAR_BASE 0x06008000u
enum { OUTER_MAP_BG2_CONTROL=0x1d09, OUTER_MASK_BG3_CONTROL=0x1f09,
       OUTER_MASK_DISPCNT=0x0800 };
static unsigned outer_mask_radius=~0u;

/* F800..FFFF belongs to BG3's map. Entries238..297 share large-font storage
 * during gameplay; AAC0..AB3F now belongs to the longer region label. */
static inline unsigned outer_map_tile(unsigned n){
    if(n<174)return 530+n;             /* C240..D7FF,174 tiles. */
    if(n<238)return 896+n-174;         /* F000..F7FF,64 tiles. */
    if(n<282)return 266+n-238;         /* A140..A6BF,44 tiles. */
    return 326+n-282;                  /* A8C0..AABF,16 tiles. */
}

static UI_FN void outer_mask_load(unsigned palette_bank,unsigned radius){
    /* Do not display a partially replaced dictionary. The draw's final
     * DISPCNT write presents the complete variant. +2 light means no mask. */
    R16(0x04000000)&=(uint16_t)~OUTER_MASK_DISPCNT;
    outer_mask_radius=radius;
    if(radius>OUTER_MASK_MAX_RADIUS)return;
    unsigned variant=radius<OUTER_MASK_MIN_RADIUS?0:radius-OUTER_MASK_MIN_RADIUS;
    const uint8_t *pixels=outer_mask_pixels[variant];
    dma(pixels,(void*)0x0600ab40,16*32);
    dma(pixels+16*32,(void*)0x0600b200,4*32);
    dma(pixels+20*32,(void*)0x0600be40,3*32);
    /* Only the first 2048 bytes are transient here. Town actor caches
     * begin at byte2048. The pending minimap commits before gameplay;
     * its next raster rebuild borrows the whole buffer at draw's tail. */
    uint16_t *map=(uint16_t*)load_buffer;
    unsigned palette=(palette_bank&15)<<12;
    for(unsigned i=0;i<1024;i++)map[i]=(uint16_t)(outer_mask_map[variant][i]|palette);
    dma(map,(void*)0x0600f800,2048);
    R16(0x0400000e)=OUTER_MASK_BG3_CONTROL;
    R16(0x0400001c)=OUTER_MASK_SCROLL_X;
    R16(0x0400001e)=OUTER_MASK_SCROLL_Y;
}

static UI_FN void outer_mask_refresh(unsigned palette_bank,unsigned radius){
    if(radius!=outer_mask_radius)outer_mask_load(palette_bank,radius);
}

/* Caller owns scene/menu/finale policy. Ordinary same-radius frames perform
 * no DMA or raster work; presentation adds only this DISPCNT bit. */
static inline unsigned outer_mask_present(unsigned enabled){
    return enabled?OUTER_MASK_DISPCNT:0;
}
