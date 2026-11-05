#ifndef PROCEDURAL_GLOBES_H
#define PROCEDURAL_GLOBES_H
#include <stdint.h>

/* BG1 uses the shared scene palette. Only these indices are retained in RAM;
 * rendering uses the existing 1024-byte decoder scratch and sixteen 8bpp tiles.
 * An optional 32-byte mask permits only immutable dungeon palette slots;
 * NULL permits the complete town palette. Prepare before gamma correction;
 * normal palette gamma updates then affect the glass and terrain consistently. */
enum { GLOBE_COLORS=21, GLOBE_HEIGHT=28, GLOBE_BYTES=1024 };
#define GLOBE_FN static __attribute__((section(".text.ui"),noinline))

GLOBE_FN void globe_palette(uint8_t out[GLOBE_COLORS],const uint16_t *palette,const uint8_t *allowed){
#define GL_RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))
    static const uint16_t target[GLOBE_COLORS]={
        GL_RGB(1,1,2), GL_RGB(4,4,5), GL_RGB(8,9,10),
        GL_RGB(10,11,12), GL_RGB(22,24,25),
        GL_RGB(1,2,3), GL_RGB(3,4,5), GL_RGB(8,10,12),
        GL_RGB(18,21,23), GL_RGB(18,19,20), GL_RGB(5,6,7),
        /* Compress the liquid ramps around darker midtones. Mana's upper
         * bands may share a static palette entry instead of becoming cyan. */
        GL_RGB(7,0,0), GL_RGB(10,0,0), GL_RGB(13,1,1),
        GL_RGB(16,1,1), GL_RGB(18,2,2),
        GL_RGB(0,0,3), GL_RGB(0,0,10), GL_RGB(0,0,17),
        GL_RGB(0,0,21), GL_RGB(1,1,23)
    };
#undef GL_RGB
    for(unsigned t=0;t<GLOBE_COLORS;t++){
        unsigned best=~0u,index=1,c=target[t];
        for(unsigned i=1;i<256;i++){
            if(allowed&&!(allowed[i>>3]&(1u<<(i&7))))continue;
            unsigned p=palette[i];
            unsigned r=p&31,g=(p>>5)&31,b=(p>>10)&31;
            /* A sparse dungeon palette must not turn red into orange or
             * saturated mana into neutral grey just to match brightness. */
            if(t>=11&&t<16&&(!r||r<4*g||r<4*b))continue;
            if(t>=16&&(!b||b<2*r||b<2*g))continue;
            int dr=(int)(c&31)-(int)(p&31);
            int dg=(int)((c>>5)&31)-(int)((p>>5)&31);
            int db=(int)((c>>10)&31)-(int)((p>>10)&31);
            unsigned distance=(unsigned)(dr*dr+dg*dg+db*db);
            if(distance<best){best=distance;index=i;}
        }
        out[t]=(uint8_t)index;
    }
}

/* Cache this visible pixel height, not the raw resource value. Positive values
 * retain a one-row sliver; a genuinely full resource alone reaches the top.
 * Reevaluate using the current maximum, including changes to maximum HP. */
GLOBE_FN unsigned globe_level(int value,int maximum){
    if(value<=0||maximum<=0)return 0;
    if(value>=maximum)return GLOBE_HEIGHT;
    unsigned level=(unsigned)value*GLOBE_HEIGHT/(unsigned)maximum;
    return level?level:1;
}

GLOBE_FN void globe_render(uint8_t *out,unsigned side,unsigned level,const uint8_t colors[GLOBE_COLORS]){
    /* Radii 16 and 14 sampled at pixel centers; mirrored vertically. No runtime
     * square roots, floating point, image decoding, or full-frame storage. */
    static const uint8_t edge[16]={12,9,7,6,5,4,3,2,2,1,1,1,0,0,0,0};
    static const uint8_t inner[16]={16,16,12,10,8,7,6,5,4,4,3,3,2,2,2,2};
    if(level>GLOBE_HEIGHT)level=GLOBE_HEIGHT;
    unsigned liquid=side?16:11,line=30-level;
    for(unsigned y=0;y<32;y++){
        unsigned row=y<16?y:31-y,left=edge[row],glass=inner[row];
        unsigned dest=(y>>3)*256+(y&7)*8;
        for(unsigned x=0;x<32;x++){
            unsigned ink=0;
            if(x>=left&&x<32-left){
                unsigned c;
                if(x<glass||x>=32-glass){
                    /* Both globes have the same continuous, colored glass
                     * edge. No grey metal bevels or segmented iron joins. */
                    c=0;
                    if(x!=left&&x!=31-left&&y!=0&&y!=31)
                        c=liquid+(y<14&&x<17);
                }else{
                    c=5;
                    if(x==glass||x==31-glass)c=0;
                    if(y>=line){
                        /* Clean curved color bands, without texture/dither.
                         * HP and MP differ only in the liquid color ramp. */
                        int dx=(int)x-21,dy=(int)y-10;
                        unsigned d=(unsigned)(dx*dx+dy*dy);
                        unsigned shade=d<38?3:d<105?2:d<245?1:0;
                        c=liquid+shade;
                        if(level<GLOBE_HEIGHT&&y==line&&x>glass&&x<31-glass)
                            c=liquid+1+(x>=14&&x<=25);
                    }else{
                        int dx=(int)x-22,dy=(int)y-8;
                        if(dx*dx+dy*dy<26)c=6;
                    }
                    /* The small glass reflection remains at the upper right,
                     * even when empty; it is not a grain/texture pattern. */
                    if((y==7&&x>=21&&x<=23)||(y==8&&(x==20||x==24))||
                       (y==10&&x>=21&&x<=23))c=y>=line?liquid+4:7;
                    if((y==8&&(x==21||x==23))||(y==9&&x>=21&&x<=23))c=3;
                    if(y==8&&x==22)c=9;
                    if(y>=23&&y<=25&&x==29-glass)c=6;
                }
                ink=colors[c];
            }
            out[dest+(x>>3)*64+(x&7)]=(uint8_t)ink;
        }
    }
}
#undef GLOBE_FN
#endif
