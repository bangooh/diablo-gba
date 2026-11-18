#ifndef RENDER_COORDS_H
#define RENDER_COORDS_H
/* Camera coordinates may be negative when the hero approaches the map edge.
 * ARM/Clang uses an arithmetic shift, so -1px belongs to tile -1, not tile 0. */
/* Inlined everywhere ARM code calls them. Clang does not inline into Thumb
 * callers, whose out-of-line copies would otherwise fall into the IWRAM
 * *(.text*) catch-all; those callers are ROM code, so keep the copies there. */
__attribute__((section(".text.ui"))) static inline int render_tile(int pixel){return pixel>>3;}
__attribute__((section(".text.ui"))) static inline unsigned render_column(int x){int r=x%31;return (unsigned)(r<0?r+31:r);}
__attribute__((section(".text.ui"))) static inline unsigned render_row(int y){int r=y%21;return (unsigned)(r<0?r+21:r)*31;}
__attribute__((section(".text.ui"))) static inline unsigned render_slot(int x,int y){return render_row(y)+render_column(x);}
/* -1 is now a valid world tile coordinate. */
enum { RING_INVALID=-32768 };
#endif
