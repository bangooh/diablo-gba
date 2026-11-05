#include "game.h"
#define RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))
const uint16_t game_palette[32]={
 RGB(2,2,4),RGB(5,5,7),RGB(8,8,10),RGB(11,11,12),RGB(15,14,13),
 RGB(5,6,8),RGB(10,11,13),RGB(16,16,18),RGB(24,22,17),RGB(31,27,16),
 RGB(10,3,4),RGB(23,6,5),RGB(31,12,5),RGB(31,23,9),RGB(7,13,20),
 RGB(11,20,29),RGB(21,23,25),RGB(29,22,15),RGB(4,12,8),RGB(8,23,13),
 RGB(13,7,17),RGB(24,12,26),RGB(31,31,28),RGB(3,3,6),RGB(18,4,5),
 RGB(30,8,8),RGB(8,8,11),RGB(15,17,20),RGB(4,9,12),RGB(8,19,23),0,0};
static volatile uint16_t *fb;
static void pixel(int x,int y,int c) {
    if((unsigned)x>=240 || (unsigned)y>=160) return;
    unsigned k=(unsigned)(y*240+x)>>1; uint16_t v=fb[k];
    fb[k]=(x&1)?(uint16_t)((v&255)|(c<<8)):(uint16_t)((v&65280)|c);
}
static void span(int x,int y,int w,int c) {
    if((unsigned)y>=160 || x>=240 || x+w<=0) return;
    if(x<0) { w+=x; x=0; } if(x+w>240) w=240-x;
    if(x&1) { pixel(x++,y,c); w--; }
    int at=(y*240+x)>>1; uint16_t color=(uint16_t)(c|(c<<8));
    while(w>=2) { fb[at++]=color; w-=2; }
    if(w) pixel(x+((at-((y*240+x)>>1))<<1),y,c);
}
static void rect(int x,int y,int w,int h,int c) {
    for(int j=0;j<h;j++) span(x,y+j,w,c);
}
static void diamond(int x,int y,int c) {
    for(int j=-6;j<=6;j++) { int a=j<0?-j:j; int w=12-2*a; span(x-w,y+j,w*2+1,c); }
}
/* Original 3x5 bitmap font, uppercase A-Z then 0-9. */
static const uint16_t font[]={
 0x5F6A,0x3B6B,0x624E,0x3B6B,0x73CF,0x13CF,0x6B4E,0x5BED,
 0x7497,0x2B24,0x5AED,0x7249,0x5B7D,0x5B6D,0x2B6A,0x13EB,
 0x6F6A,0x5AEB,0x388E,0x2497,0x6B6D,0x2B6D,0x5F6D,0x5AAD,
 0x24AD,0x72A7,0x7B6F,0x749A,0x73E7,0x79E7,0x49ED,0x79CF,
 0x7BCF,0x4927,0x7BEF,0x79EF};
static void text(int x,int y,const char *s,int c) {
    for(;*s;s++,x+=4) {
        int index=(*s>='A' && *s<='Z')?*s-'A':(*s>='0' && *s<='9')?26+*s-'0':-1;
        if(index<0) continue;
        uint16_t bits=font[index];
        for(int j=0;j<5;j++) for(int i=0;i<3;i++) if(bits&(1<<(j*3+i))) pixel(x+i,y+j,c);
    }
}
static void number(int x,int y,int n,int c) {
    char s[4]; int h=0,t=0; while(n>=100){n-=100;h++;}while(n>=10){n-=10;t++;}
    s[0]=(char)('0'+h);s[1]=(char)('0'+t);s[2]=(char)('0'+n);s[3]=0;text(x,y,s,c);
}
static void actor(int x,int y,int body,int enemy,int flash) {
    span(x-5,y+2,11,0); rect(x-4,y-2,3,5,5);rect(x+2,y-2,3,5,5);
    rect(x-5,y-10,11,9,flash?22:body);
    rect(x-3,y-15,7,6,enemy?18:17);
    if(enemy) {pixel(x-2,y-13,11);pixel(x+2,y-13,11);}
    else {rect(x-3,y-16,7,3,7);rect(x+7,y-12,2,13,16);rect(x+5,y-2,6,2,9);}
}
void render_game(const Game *g,volatile uint16_t *pixels) {
    fb=pixels; for(int i=0;i<19200;i++) fb[i]=0;
    int camx=(g->x-g->y)*12,camy=(g->x+g->y)*6;
    for(int d=0;d<MAP_W+MAP_H-1;d++) for(int y=0;y<MAP_H;y++) {
        int x=d-y; if(x<0||x>=MAP_W)continue;
        int sx=120+(x-y)*12-camx,sy=82+d*6-camy;
        if(sx<-15||sx>255||sy<5||sy>161)continue;
        if(g->map[y][x]) {
            diamond(sx,sy,2+((x+y)&1));
            span(sx-4,sy,3,((x*3+y)&3)?2:4);
            if(g->map[y][x]==2) {
                for(int j=0;j<4;j++)span(sx-7+j,sy-3+j*2,14-j*2,g->kills==12?19:7);
            }
        } else {
            int adjacent=game_walkable(g,x-1,y)||game_walkable(g,x+1,y)||
                game_walkable(g,x,y-1)||game_walkable(g,x,y+1);
            if(!adjacent)continue;
            /* Low wall blocks keep the player visible; no original Diablo art. */
            diamond(sx,sy,5);rect(sx-10,sy-8,21,8,5);
            diamond(sx,sy-8,6);span(sx-5,sy-8,10,7);
        }
        for(int i=0;i<MAX_DROPS;i++) if(g->drops[i].active && g->drops[i].x==x && g->drops[i].y==y) {
            int c=g->drops[i].kind?11:9;rect(sx-2,sy-4,5,4,c);pixel(sx,sy-5,22);
        }
        for(int i=0;i<MAX_ENEMIES;i++) if(g->enemies[i].alive && g->enemies[i].x==x && g->enemies[i].y==y) {
            actor(sx,sy,g->enemies[i].kind?20:18,1,0);
            if(g->enemies[i].hp<30)span(sx-5,sy-19,5,11);
        }
        if(g->x==x&&g->y==y) {
            actor(sx,sy,14,0,g->hit_flash);
            if(g->flash) {span(sx+9,sy-17,8,22);span(sx+13,sy-15,7,9);span(sx+16,sy-12,5,22);}
        }
    }
    rect(0,0,240,19,23); text(7,4,"DUNGEON",9); text(42,4,"GBA LAB",7);
    text(7,12,"HP",16); rect(20,12,52,5,10);rect(21,13,g->hp>>1,3,g->hp<30?25:11);
    text(84,4,"GOLD",8);number(105,4,g->gold,9);
    text(84,12,"POT",16);number(105,12,g->potions,19);
    text(141,4,"KILLS",16);number(166,4,g->kills,22);text(182,4,"OF 12",7);
    text(141,12,"FLOOR 01",8);
    rect(0,146,240,14,23);text(5,151,"A ATTACK  B HEAL  START RESET  SELECT PAUSE",7);
    if(g->won||g->hp<=0||g->paused) {
        rect(42,61,156,36,23);rect(43,62,154,1,9);
        text(88,71,g->won?"FLOOR CLEAR":g->hp<=0?"YOU DIED":"PAUSED",22);
        text(77,84,g->paused?"SELECT TO RESUME":"START TO RESTART",9);
    }
}
