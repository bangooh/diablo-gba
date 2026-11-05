/* Normal dungeon cache; Stage80 overflow uses bounded streaming. */
static inline unsigned map_seen(const uint8_t *fog,int x,int y){
    if(game.area)return 1;
    if(x<0||y<0||x>=80||y>=80)return 0;
    unsigned n=(unsigned)(y/2)*40+(unsigned)x/2;return (fog[n>>3]>>(n&7))&1;
}
static UI_FN void map_line(int ax,int ay,int bx,int by){
    int ox=(map_screen_fallback&1)?120-map_point_x(game.x,game.y):map_origin_x(game.x,game.y);
    int oy=(map_screen_fallback&1)?80-map_point_y(game.x,game.y):map_origin_y(game.x,game.y);
    unsigned width=(map_screen_fallback&1)?240:512,stride=width/8;
    int begin=map_point_x(ax,ay),end=map_point_x(bx,by);
    if(begin>end){int old=begin;begin=end;end=old;}
    int axis=ax==bx,k=map_vertex(axis?ax:ay);
    /* Raster the exact1:2 diagonal, independent of a run's length or where
     * fog splits it. Endpoint-rounded Bresenham invents many slight slopes,
     * exhausting the tile dictionary on the enlarged map. */
    int intercept=4*k-4;
    int left=(map_screen_fallback&1)?0:map_screen_fallback==2?96:64,right=(map_screen_fallback&1)?240:map_screen_fallback==2?416:448;
    unsigned top=(map_screen_fallback&1)?0:map_screen_fallback==2?24:16,bottom=(map_screen_fallback&1)?160:map_screen_fallback==2?232:240;
    if(begin<left-ox)begin=left-ox;if(end>=right-ox)end=right-ox-1;
    /* y=oy+floor((+/-x+intercept)/2). Clip that exact equation before
     * stepping, so contours above/below the cached window cost no pixels. */
    int low=2*((int)top-oy)-intercept,high=2*((int)bottom-oy)-1-intercept;
    if(axis){int old=low;low=-high;high=-old;}
    if(begin<low)begin=low;if(end>high)end=high;
    for(int wx=begin;wx<=end;wx++){
        int x=ox+wx;unsigned y=(unsigned)((axis?-wx:wx)+intercept+2*oy)>>1;
        unsigned pos=((unsigned)y/8*stride+(unsigned)x/8)*8+(y&7);MAP_RASTER[pos]|=(uint8_t)(1u<<(x&7));
    }
}
static UI_FN void automap_rasterize(void){
    uint8_t fog[200];if(!game.area)game_explore_export(fog);
    /* Fog is a 40x40 cell bitmap. Restrict each contour row to its revealed
     * span instead of testing every edge of the entire dungeon on discovery.
     * Include the terminating endpoint so merged runs retain exact pixels. */
    uint8_t bounds[2][MAP_GRID][2];
    if(!game.area){
        for(unsigned axis=0;axis<2;axis++)for(unsigned row=0;row<MAP_GRID;row++){
            bounds[axis][row][0]=MAP_GRID;bounds[axis][row][1]=0;
        }
        for(unsigned byte=0;byte<200;byte++)if(fog[byte])for(unsigned bit=0;bit<8;bit++)if(fog[byte]&(1u<<bit)){
            unsigned cell=byte*8+bit,x=(cell%40)*2,y=(cell/40)*2;
            for(unsigned d=0;d<3;d++){
                uint8_t *h=bounds[0][y+d],*v=bounds[1][x+d];
                if(x<h[0])h[0]=(uint8_t)x;if(x+2>h[1])h[1]=(uint8_t)(x+2);
                if(y<v[0])v[0]=(uint8_t)y;if(y+2>v[1])v[1]=(uint8_t)(y+2);
            }
        }
    }
    unsigned bytes=(map_screen_fallback&1)?4800:16384;
    for(unsigned i=0;i<bytes/4;i++)((uint32_t*)MAP_RASTER)[i]=0;
    /* Merge collinear unit edges before drawing, preserving a single-pixel
     * diagonal even when projection rounds several short edges differently. */
    for(unsigned axis=0;axis<2;axis++)for(unsigned row=0;row<MAP_GRID;row++){
        const uint8_t *edges=axis?MAP_REVEALED:MAP_OUTLINE;int start=-1;
        unsigned begin=game.area?0:bounds[axis][row][0],end=game.area?96:bounds[axis][row][1];
        for(unsigned col=begin;col<=end;col++){
            if(start<0&&col<=88){unsigned n=row*MAP_GRID+col;
                if(!(((edges[n>>3]|((unsigned)edges[(n>>3)+1]<<8))>>(n&7))&255)){col+=7;continue;}}
            unsigned x=axis?row:col,y=axis?col:row;
            unsigned visible=col<96&&map_edge(edges,col,row)&&
                (map_seen(fog,(int)x,(int)y)||map_seen(fog,(int)x-(int)axis,(int)y-(int)!axis));
            if(visible&&start<0)start=(int)col;
            if(!visible&&start>=0){
                if(axis)map_line((int)row,start,(int)row,(int)col);
                else map_line(start,(int)row,(int)col,(int)row);
                start=-1;
            }
        }
    }
}
static inline uint32_t map_flip_x(uint32_t v){
    v=((v>>1)&0x55555555u)|((v&0x55555555u)<<1);
    v=((v>>2)&0x33333333u)|((v&0x33333333u)<<2);
    return ((v>>4)&0x0f0f0f0fu)|((v&0x0f0f0f0fu)<<4);
}
static inline uint32_t map_flip_y(uint32_t v){
    return (v>>24)|((v>>8)&0xff00u)|((v&0xff00u)<<8)|(v<<24);
}
static UI_FN unsigned automap_pack(void){
    const uint32_t *words=(const uint32_t*)MAP_RASTER;
    uint32_t *dictionary=MAP_DICTIONARY_BITS;
    uint16_t *refs=MAP_REFS,slots[512];unsigned count=1;
    dictionary[0]=dictionary[1]=0;
    for(unsigned i=0;i<512;i++)slots[i]=0;
    unsigned viewport=map_screen_fallback&1,width=viewport?30:64,rows=viewport?20:32;
    unsigned left=viewport?0:map_screen_fallback==2?12:8,right=viewport?30:map_screen_fallback==2?52:56;
    unsigned top=viewport?0:map_screen_fallback==2?3:2,bottom=viewport?20:map_screen_fallback==2?29:30;
    unsigned limit=viewport?MAP_FALLBACK_DICTIONARY:MAP_DICTIONARY;
    /* The raster's padding is known transparent. Clear its references in
     * forward order: refs overlap the unread raster, so an up-front memset
     * would destroy active pixels. Keep active tiles in the original order. */
    for(unsigned y=0;y<rows;y++){
        unsigned begin=y*width,end=begin+width;
        if(y<top||y>=bottom){for(unsigned t=begin;t<end;t+=2)((uint32_t*)refs)[t/2]=0;continue;}
        for(unsigned t=begin;t<begin+left;t+=2)((uint32_t*)refs)[t/2]=0;
        for(unsigned t=begin+left;t<begin+right;t++){
        uint32_t a=words[t*2],b=words[t*2+1];unsigned n=0,flip=0;
        if(a||b){
            uint32_t original_a=a,original_b=b;
            for(unsigned f=1;f<4;f++){
                uint32_t h=(f&1)?map_flip_x(original_a):original_a;
                uint32_t j=(f&1)?map_flip_x(original_b):original_b;
                if(f&2){uint32_t old=h;h=map_flip_y(j);j=map_flip_y(old);}
                if(h<a||(h==a&&j<b)){a=h;b=j;flip=f<<10;}
            }
            uint32_t hash=a^(b*0x9e3779b9u);hash^=hash>>16;hash*=0x85ebca6bu;hash^=hash>>16;
            unsigned slot=hash&511;
            while(slots[slot]){n=slots[slot];if(dictionary[n*2]==a&&dictionary[n*2+1]==b)break;slot=(slot+1)&511;}
            if(!slots[slot]){
                /* Never publish a partially missing contour on exhaustion. */
                if(count>=limit){map_tile_overflows++;return 0;}
                n=count++;dictionary[n*2]=a;dictionary[n*2+1]=b;slots[slot]=(uint16_t)n;
            }
        }
        refs[t]=(uint16_t)(n|flip);
        }
        for(unsigned t=begin+right;t<end;t+=2)((uint32_t*)refs)[t/2]=0;
    }
    map_unique_tiles=count;return 1;
}
static UI_FN void automap_prepare_tiles(void){
    uint16_t lut[16];
    for(unsigned n=0;n<16;n++){
        unsigned v=0;for(unsigned b=0;b<4;b++)if(n&(1u<<b))v|=(map_line_color&15)<<(b*4);
        lut[n]=(uint16_t)v;
    }
    const uint8_t *bits=(const uint8_t*)MAP_DICTIONARY_BITS;
    uint32_t *dst=MAP_UPLOAD_TILES;
    for(unsigned t=0;t<map_unique_tiles;t++)for(unsigned row=0;row<8;row++){
        unsigned n=bits[t*8+row];*dst++=lut[n&15]|((uint32_t)lut[n>>4]<<16);
    }
}
static UI_FN void automap_prepare_upload(void){
    automap_prepare_tiles();
    unsigned palette=(map_line_color>>4)<<12,first=530|palette;
    uint32_t blank=first|(first<<16),palettes=palette|(palette<<16);
    const uint32_t *refs=(const uint32_t*)MAP_REFS;
    uint32_t *out=(uint32_t*)MAP_UPLOAD_REFS;
    if(map_screen_fallback&1){
        for(unsigned y=0;y<20;y++){
            for(unsigned x=0;x<15;x++){
                uint32_t pair=*refs++;
                *out++=map_unique_tiles<=174?pair+blank:
                    map_tile(pair&1023)|(map_tile((pair>>16)&1023)<<16)|(pair&0x0c000c00u)|palettes;
            }
            *out++=blank;
        }
        for(unsigned i=0;i<12*16;i++)*out++=blank;
    }else if(map_unique_tiles<=174){
        /* Every dictionary index fits the first contiguous VRAM bank. The
         * addition cannot carry into flips or the next halfword. Convert two
         * refs at once while splitting each row into the two screen blocks. */
        for(unsigned y=0;y<32;y++,refs+=32,out+=16)for(unsigned x=0;x<16;x++){
            out[x]=refs[x]+blank;out[x+512]=refs[x+16]+blank;
        }
    }else{
        for(unsigned y=0;y<32;y++,refs+=32,out+=16)for(unsigned x=0;x<16;x++){
            uint32_t a=refs[x],b=refs[x+16];
            out[x]=map_tile(a&1023)|(map_tile((a>>16)&1023)<<16)|(a&0x0c000c00u)|palettes;
            out[x+512]=map_tile(b&1023)|(map_tile((b>>16)&1023)<<16)|(b&0x0c000c00u)|palettes;
        }
    }
}
static inline unsigned map_live_ref(unsigned tile){
    unsigned at=((tile&63)>>5)*1024+(tile>>6)*32+(tile&31);
    return (MAP_VRAM_WORD(0x0600e800u+(at&~1u)*2)>>(16*(at&1)))&65535;
}
/* Decode one physical4bpp tile to its displayed1bpp shape, including the
 * reference flips. Partial updates never assume canonical tiles are unique. */
static UI_FN void map_read_live_tile(unsigned ref,uint32_t *out){
    uint32_t a=0,b=0;
    for(unsigned row=0;row<8;row++){
        uint32_t pixels=MAP_VRAM_WORD(0x06008000u+(ref&1023)*32+row*4);unsigned bits=0;
        for(unsigned x=0;x<8;x++)if((pixels>>(x*4))&15)bits|=1u<<x;
        if(row<4)a|=bits<<(row*8);else b|=bits<<((row-4)*8);
    }
    if(ref&1024){a=map_flip_x(a);b=map_flip_x(b);}
    if(ref&2048){uint32_t old=a;a=map_flip_y(b);b=map_flip_y(old);}
    out[0]=a;out[1]=b;
}
static UI_FN unsigned map_patch_line(int begin,int end,int intercept,unsigned axis,int ox,int oy,uint32_t *dirty){
    if(begin>end){int old=begin;begin=end;end=old;}
    int left=map_screen_fallback==2?96:64,right=map_screen_fallback==2?416:448;
    int top=map_screen_fallback==2?24:16,bottom=map_screen_fallback==2?232:240;
    if(begin<left-ox)begin=left-ox;if(end>=right-ox)end=right-ox-1;
    int low=2*(top-oy)-intercept,high=2*(bottom-oy)-1-intercept;
    if(axis){int old=low;low=-high;high=-old;}
    if(begin<low)begin=low;if(end>high)end=high;
    unsigned changed=0;
    for(int wx=begin;wx<=end;wx++){
        unsigned x=(unsigned)(ox+wx),y=(unsigned)((axis?-wx:wx)+intercept+2*oy)>>1;
        unsigned tile=(y/8)*64+x/8,word=tile>>5,mask=1u<<(tile&31);
        if(!(dirty[word]&mask)){map_read_live_tile(map_live_ref(tile),((uint32_t*)MAP_RASTER)+tile*2);dirty[word]|=mask;}
        unsigned at=tile*8+(y&7),bit=1u<<(x&7);
        if(!(MAP_RASTER[at]&bit)){MAP_RASTER[at]|=(uint8_t)bit;changed=1;}
    }
    return changed;
}
static UI_FN unsigned automap_pack_dirty(const uint32_t *dirty){
    uint16_t slots[512];uint32_t *dictionary=MAP_DICTIONARY_BITS;
    unsigned count=map_unique_tiles;
    for(unsigned i=0;i<512;i++)slots[i]=0;
    /* Dictionary IDs remain stable so every untouched live reference stays
     * valid. Reconstruct canonical patterns from the currently committed VRAM. */
    for(unsigned n=0;n<count;n++){
        map_read_live_tile(map_tile(n),dictionary+n*2);
        if(n){uint32_t hash=dictionary[n*2]^(dictionary[n*2+1]*0x9e3779b9u);hash^=hash>>16;hash*=0x85ebca6bu;hash^=hash>>16;
            unsigned slot=hash&511;while(slots[slot])slot=(slot+1)&511;slots[slot]=(uint16_t)n;}
    }
    /* Consume raster tiles before overwriting their lower-address refs. Do
     * not expand dictionary pixels or stage live screen blocks until done. */
    for(unsigned word=0;word<64;word++)if(dirty[word])for(unsigned bit=0;bit<32;bit++)if(dirty[word]&(1u<<bit)){
        unsigned tile=word*32+bit,flip=0,n=0;uint32_t a=((uint32_t*)MAP_RASTER)[tile*2],b=((uint32_t*)MAP_RASTER)[tile*2+1];
        if(a||b){
            uint32_t original_a=a,original_b=b;
            for(unsigned f=1;f<4;f++){
                uint32_t h=(f&1)?map_flip_x(original_a):original_a,j=(f&1)?map_flip_x(original_b):original_b;
                if(f&2){uint32_t old=h;h=map_flip_y(j);j=map_flip_y(old);}
                if(h<a||(h==a&&j<b)){a=h;b=j;flip=f<<10;}
            }
            uint32_t hash=a^(b*0x9e3779b9u);hash^=hash>>16;hash*=0x85ebca6bu;hash^=hash>>16;unsigned slot=hash&511;
            while(slots[slot]){n=slots[slot];if(dictionary[n*2]==a&&dictionary[n*2+1]==b)break;slot=(slot+1)&511;}
            if(!slots[slot]){if(count>=MAP_DICTIONARY)return 0;n=count++;dictionary[n*2]=a;dictionary[n*2+1]=b;slots[slot]=(uint16_t)n;}
        }
        MAP_REFS[tile]=(uint16_t)(n|flip);
    }
    map_unique_tiles=count;return 1;
}
static UI_FN unsigned automap_cache_contains_view(void){
    int x=map_origin_x(map_last_x,map_last_y)+map_ref_x-120;
    int y=map_origin_y(map_last_x,map_last_y)+map_ref_y-80;
    return map_screen_fallback==2?x>=112&&x<=160&&y>=32&&y<=64:x>=80&&x<=192&&y>=24&&y<=72;
}
static UI_FN unsigned automap_incremental(unsigned revision){
    if(game.area||map_init||map_pending||map_last_x==-1||map_last_area!=game.area||
       (map_screen_fallback&1)||map_screen_fallback!=map_screen_live||!automap_cache_contains_view())return 0;
    uint32_t fog_words[50],dirty[64];uint8_t *fog=(uint8_t*)fog_words;unsigned added=0,changed=0;
    game_explore_export(fog);
    for(unsigned i=0;i<200;i++){
        if(MAP_OLD_FOG[i]&~fog[i])return 0;
        unsigned bits=fog[i]&~MAP_OLD_FOG[i];while(bits){bits&=bits-1;if(++added>24)return 0;}
    }
    for(unsigned i=0;i<64;i++)dirty[i]=0;
    int ox=map_origin_x(map_last_x,map_last_y),oy=map_origin_y(map_last_x,map_last_y);
    for(unsigned byte=0;byte<200;byte++){
        unsigned bits=fog[byte]&~MAP_OLD_FOG[byte];
        for(unsigned bit=0;bits&&bit<8;bit++)if(bits&(1u<<bit)){
            bits&=~(1u<<bit);unsigned cell=byte*8+bit,x=(cell%40)*2,y=(cell/40)*2;
            int u[3],v[3];for(unsigned d=0;d<3;d++){u[d]=map_vertex((int)(x+d));v[d]=map_vertex((int)(y+d));}
            for(unsigned d=0;d<3;d++)for(unsigned e=0;e<2;e++){
                if(map_edge(MAP_OUTLINE,x+e,y+d)&&!map_seen(MAP_OLD_FOG,(int)(x+e),(int)(y+d))&&!map_seen(MAP_OLD_FOG,(int)(x+e),(int)(y+d)-1))
                    changed|=map_patch_line(2*(u[e]-v[d]),2*(u[e+1]-v[d]),4*v[d]-4,0,ox,oy,dirty);
                if(map_edge(MAP_REVEALED,y+e,x+d)&&!map_seen(MAP_OLD_FOG,(int)(x+d),(int)(y+e))&&!map_seen(MAP_OLD_FOG,(int)(x+d)-1,(int)(y+e)))
                    changed|=map_patch_line(2*(u[d]-v[e]),2*(u[d]-v[e+1]),4*u[d]-4,1,ox,oy,dirty);
            }
        }
    }
    if(changed){
        if(!automap_pack_dirty(dirty))return 0;
        /* All dirty pixels have now been consumed; staging can safely reuse
         * the raster. Untouched hardware references are copied verbatim. */
        automap_prepare_tiles();dma((const void*)0x0600e800,MAP_UPLOAD_REFS,4096);
        unsigned palette=(map_line_color>>4)<<12;
        for(unsigned word=0;word<64;word++)if(dirty[word])for(unsigned bit=0;bit<32;bit++)if(dirty[word]&(1u<<bit)){
            unsigned tile=word*32+bit,ref=MAP_REFS[tile],at=((tile&63)>>5)*1024+(tile>>6)*32+(tile&31);
            MAP_UPLOAD_REFS[at]=(uint16_t)(map_tile(ref&1023)|(ref&0xc00)|palette);
        }
        map_pending=2;
    }
    for(unsigned i=0;i<50;i++)((uint32_t*)MAP_OLD_FOG)[i]=fog_words[i];
    map_last_revision=revision;return 1;
}
static UI_FN void automap_cached_draw(void){
    if(!map_visible)return;
    unsigned revision=game_explore_revision();
    if(map_last_x!=-1&&map_last_area==game.area&&map_last_revision==revision){
        if(map_screen_fallback&1){if(map_last_x==game.x&&map_last_y==game.y)return;}
        /* Margins also cover one outstanding interpolated movement. */
        else if(automap_cache_contains_view())return;
    }
    if(automap_incremental(revision))return;
    if(map_init||map_last_area!=game.area){automap_build_outline();map_screen_fallback=0;}
    /* This one-shot rebuild borrows all loading scratch. Actor rendering is
     * already complete; invalidate the four town caches before overwriting. */
    for(unsigned i=0;i<4;i++)raw_base[i]=0;
    automap_rasterize();
    if(!automap_pack()){
        /* Keep the fast wide cache for normal layouts. Overflow switches to
         * bounded ring jobs once, rather than rebuilding a viewport every step. */
        map_stream_active=1;map_last_x=-1;map_init=1;map_pending=0;
        automap_stream_draw();return;
    }
    automap_prepare_upload();
    if(!game.area)game_explore_export(MAP_OLD_FOG);
    map_last_x=game.x;map_last_y=game.y;map_last_area=game.area;map_last_revision=revision;
    map_pending=2;
}
