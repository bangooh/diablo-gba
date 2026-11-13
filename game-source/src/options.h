#ifndef GAME_OPTIONS_H
#define GAME_OPTIONS_H
#include "audio.h"
#include "game.h"

/* Keep the first ten brightness settings and the neutral default unchanged.
 * New settings extend the same saturating RGB555 multiplier to twenty steps. */
enum { OPTIONS_GAMMA_LEVELS=20, OPTIONS_VOLUME_TAG=0x534e4400u };
/* Only the current D-pad setting is stored; no removed display-mode bit. */
enum { OPTIONS_DPAD_TAG=0x44504100u };
enum { OPTIONS_DPAD_ORIGINAL,OPTIONS_DPAD_CCW,OPTIONS_DPAD_CW,OPTIONS_DPAD_COUNT };
static const char *const options_dpad_labels[]={"Original","CCW-45degree","CW-45degree"};
static inline unsigned options_dpad_valid(unsigned saved){return (saved&~3u)==OPTIONS_DPAD_TAG&&(saved&3u)<OPTIONS_DPAD_COUNT;}
static inline unsigned options_dpad_pack(unsigned rotation){return OPTIONS_DPAD_TAG|rotation;}
__attribute__((section(".text.ui"))) static inline unsigned options_dpad_unpack(unsigned saved){return options_dpad_valid(saved)?saved&3u:OPTIONS_DPAD_ORIGINAL;}
/* Rotate screen directions, including diagonals, before gameplay or local
 * wire encoding. Never run this on UI navigation or packed item commands. */
static __attribute__((section(".text.ui"),noinline)) unsigned options_dpad_keys(unsigned held,unsigned rotation){
 if(rotation==OPTIONS_DPAD_ORIGINAL||rotation>=OPTIONS_DPAD_COUNT)return held;
 int x=!!(held&KEY_RIGHT)-!!(held&KEY_LEFT),y=!!(held&KEY_DOWN)-!!(held&KEY_UP);
 int dx=rotation==OPTIONS_DPAD_CCW?x+y:x-y,dy=rotation==OPTIONS_DPAD_CCW?y-x:y+x;
 return (held&~(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))|
        (dx>0?KEY_RIGHT:dx<0?KEY_LEFT:0)|(dy>0?KEY_DOWN:dy<0?KEY_UP:0);
}
static inline unsigned options_volume_pack(unsigned music,unsigned effects){
 return OPTIONS_VOLUME_TAG|music|(effects<<4);
}
static __attribute__((section(".text.ui"),noinline)) int options_volume_valid(unsigned saved){
 unsigned music=saved&15u,effects=(saved>>4)&15u;
 return !saved||((saved&~255u)==OPTIONS_VOLUME_TAG&&music<=10&&effects>=1&&effects<=10);
}
static inline unsigned options_music_unpack(unsigned saved){
 if(!saved)return 5;
 return (saved&15u)?saved&15u:1;
}
static inline unsigned options_effects_unpack(unsigned saved){return saved?(saved>>4)&15u:5;}
static __attribute__((section(".text.ui"),noinline)) void options_volume_apply(unsigned saved){
 /* Music zero is the current format's mute value; volume step1 is its UI value. */
 audio_music_enabled=!saved||!!(saved&15u);
 audio_effects_enabled=1;
 audio_set_music_volume(options_music_unpack(saved));
 audio_set_effects_volume(options_effects_unpack(saved));
}
#endif
