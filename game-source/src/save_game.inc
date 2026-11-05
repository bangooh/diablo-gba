/* World snapshot:lossless dictionary-packed floor cells and12-byte drops,128 item slots per world.
 * Game and Flash representations deliberately have different map sizes. */
#include <stddef.h>
typedef struct {
    uint8_t map[5728];
    Enemy enemies[MAX_ENEMIES];
    Drop drops[MAX_DROPS];
    int16_t x,y,hp,gold,kills,potions;
    uint16_t ticks,move_wait,attack_wait,flash,hit_flash;
    uint8_t won,paused;
    uint8_t facing,attack_queued;
    int8_t target,swing_target;
    uint8_t enemy_attack[MAX_ENEMIES],enemy_facing[MAX_ENEMIES];
    int16_t mana,max_mana;
    uint8_t pickup_notice,pickup_notice_ticks;
    uint8_t area,town_message;
    uint32_t belt;
    Prop props[MAX_PROPS];
    uint8_t down_x,down_y;
} PackedGame;
_Static_assert(sizeof(Game)-sizeof(PackedGame)==672,"map packing layout");
/*800 groups of8 seven-bit dictionary indexes, followed by128 byte values.
 * Reject over128 distinct codes rather than discarding architecture information. */
static UI_FN int pack_map(uint8_t *dst,const uint8_t *src){
 uint8_t indices[256],dictionary[128];unsigned count=0;
 for(int i=0;i<256;i++)indices[i]=255;
 for(int i=0;i<6400;i++)if(indices[src[i]]==255){if(count==128)return 0;indices[src[i]]=count;dictionary[count++]=src[i];}
 for(int b=0;b<800;b++){
  uint8_t c[8];for(int j=0;j<8;j++)c[j]=indices[src[b*8+j]];
  for(int j=0;j<7;j++)dst[b*7+j]=(c[j]>>j)|(c[j+1]<<(7-j));
 }
 for(int i=0;i<128;i++)dst[5600+i]=i<(int)count?dictionary[i]:0;
 return 1;
}
static UI_FN int pack_game(PackedGame *out,const Game *g){
 if(!pack_map(out->map,(const uint8_t*)g->map))return 0;
 const uint8_t *src=(const uint8_t*)g+6400;uint8_t *dst=(uint8_t*)out+5728;
 for(unsigned i=0;i<sizeof(Game)-6400;i++)dst[i]=src[i];
 return 1;
}
static UI_FN void unpack_game(Game *out,const PackedGame *g){
 uint8_t *map=(uint8_t*)out->map;
 for(int i=0;i<6400;i++){unsigned bit=i*7,n=bit/8,v=g->map[n]|(g->map[n+1]<<8);map[i]=g->map[5600+((v>>(bit&7))&127)];}
 const uint8_t *src=(const uint8_t*)g+5728;uint8_t *dst=(uint8_t*)out+6400;
 for(unsigned i=0;i<sizeof(Game)-6400;i++)dst[i]=src[i];
}
