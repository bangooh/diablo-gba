#include "game.h"
#define REG16(a) (*(volatile uint16_t *)(a))
Game game;
int main(void) {
    REG16(0x04000204)=0x4317;
    REG16(0x04000000)=0x0404;
    for(int i=0;i<32;i++) REG16(0x05000000+i*2)=game_palette[i];
    game_init(&game);
    unsigned prev=0,page=1;
    for(;;) {
        unsigned held=(~REG16(0x04000130))&1023;
        game_step(&game,held,held&~prev);prev=held;
        render_game(&game,(volatile uint16_t *)(0x06000000+page*0xA000));
        while(REG16(0x04000006)>=160) {}
        while(REG16(0x04000006)<160) {}
        REG16(0x04000000)=(uint16_t)(0x0404|(page<<4));page^=1;
    }
}
