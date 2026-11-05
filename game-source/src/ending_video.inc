/* Sequential Sorcerer ending frames. The caller retains an aligned14040-byte
 * packed frame in existing ending workspace and decodes every intervening
 * frame before presenting an audio-clock target. No new persistent RAM.
 * Include after dma(), load_buffer, UI_FN, and lz_decode are available. */
#include "../generated/ending_video.h"
static UI_FN int ending_video_frame(unsigned frame,uint32_t *packed){
    if(frame>=ENDING_VIDEO_FRAMES)return 0;
    const uint32_t *refs=(const uint32_t*)(asset_ending_video+ENDING_VIDEO_INDEX_OFFSET);
    unsigned ref=refs[frame];
    if(!ref)return 1; /* The complete previous packed frame is repeated. */
    if(lz_decode(asset_ending_video+(ref&ENDING_VIDEO_OFFSET_MASK),load_buffer,
                 sizeof(load_buffer))!=ENDING_VIDEO_PACKED_BYTES)return 0;
    if(ref&ENDING_VIDEO_DELTA_FLAG){
        for(unsigned i=0;i<ENDING_VIDEO_PACKED_BYTES/4;i++)packed[i]^=load_buffer[i];
    }else dma(load_buffer,packed,ENDING_VIDEO_PACKED_BYTES);
    return 1;
}
