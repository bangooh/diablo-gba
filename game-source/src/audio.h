#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
void audio_stop(void);
void audio_start(unsigned track);
void audio_update(void);
int audio_finale_busy(void);
/* Non-looping complete native ending audio:0 Sorcerer,1 common epilogue.
 * Shares Timer1/2/3 and FIFO A with music. Do not reconfigure these timers
 * through the frontend sound/movie helpers during ending playback. */
/* Nearest Timer1 divisor:16777216/2731=6143.25009Hz (-0.0122%).
 * Music/ordinary voices share6144Hz. Gold uses Timer0/FIFO B at8kHz. */
enum { AUDIO_SAMPLE_RATE=6144,AUDIO_TIMER_RELOAD=0xf555,
       AUDIO_ENDING_SAMPLE_RATE=AUDIO_SAMPLE_RATE,AUDIO_MUSIC_TRACKS=6 };
static inline unsigned audio_region_track(unsigned town,unsigned floor){
 return town?0:floor<=4?1:floor<=8?3:floor<=12?4:5;
}
void audio_ending_start(unsigned track);
unsigned audio_ending_position(void); /* Actually played output samples, clamped at EOF. */
int audio_ending_busy(void);
/* Native town greetings, cow interaction and first dungeon-entry lines. FIFO B runs alongside unchanged BGM;
 * the full dialogue catalog's other voice ordinals are deliberately silent.
 * The workspace arguments keep the UI contract but no borrowed RAM is needed. */
enum { AUDIO_SPEECH_WORKSPACE_BYTES=0 };
int audio_speech_start(unsigned voice,void *workspace,unsigned bytes);
void audio_speech_stop(void);
void audio_cow(unsigned cls);
void audio_speech_poll(void);
int audio_speech_active(void);
/* Original first-encounter IDs from game.h. Shares the independent item
 * channel; combat remains audible and queued loot follows the introduction. */
void audio_boss_intro(unsigned id);
int audio_boss_busy(void);
/* cls: Warrior0/Rogue1/Sorcerer2; reason: mana0/inventory-space1.
 * Two-second cooldown, original variants, drops while another voice owns
 * the item channel. Combat remains independent; no gameplay RNG is used. */
void audio_hero_error(unsigned cls,unsigned reason);
void audio_hero_death(unsigned cls);
unsigned audio_game_clock(void); /*16,384-Hz virtual Timer0, stable during8-kHz gold. */
void audio_effect(unsigned id,unsigned priority);
void audio_spell(unsigned spell);
void audio_quest_complete(void);
void audio_hazard(unsigned type);
void audio_monster(unsigned kind,unsigned action); /* attack=0, hit=1, death=2 */
/* Single voices use ROM DMA2; overlapping item/combat uses the bounded CPU
 * mixer. Music block DMA requires IRQ service within12ms; call audio_stop
 * before a longer interrupt-masked operation (including Flash/loading).
 * Independent item voice can overlap combat/death; queued loot coalesces. */
void audio_queue_item(unsigned kind,unsigned pickup);
enum { AUDIO_VOLUME_MIN=1, AUDIO_MUSIC_VOLUME_MAX=10, AUDIO_MUSIC_VOLUME_DEFAULT=5,
       AUDIO_EFFECTS_VOLUME_MAX=10, AUDIO_EFFECTS_VOLUME_DEFAULT=5 };
/* Ten audible levels. Music5 is20% above the Stage46 default; upper levels
 * use remaining PCM headroom. Sound5 is unchanged; other levels use the
 * existing bounded mixer. Adjusting either preserves the sample position. */
void audio_set_music_volume(unsigned level);
void audio_set_effects_volume(unsigned level);
extern unsigned audio_music_volume,audio_effects_volume;
extern unsigned audio_music_enabled,audio_effects_enabled;
/* Tracks: town0, cathedral1, title/main-menu2, catacombs3, caves4, hell5.
 * Tracks and ordinary voices use6144Hz; gold is8kHz. Title uses IMA4, town packed PCM,
 * the four dungeon tracks remain uncompressed PCM8.
 * it is never selected during town/dungeon gameplay. Effects: swing0, heroes1..3,
 * then monster attack/hit/death in groups of3 from4. */
#endif
