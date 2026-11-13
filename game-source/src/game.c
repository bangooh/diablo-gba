#include "game.h"
#ifdef GRAPHICS_ASSETS
#include "coop.h"
#endif
static int abs_i(int a) { return a<0?-a:a; }
#ifndef GRAPHICS_ASSETS
static int sign(int a) { return (a>0)-(a<0); }
#endif
static void room(Game *g,int x,int y,int w,int h) {
    for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) g->map[j][i]=1;
}
int game_walkable(const Game *g,int x,int y) {
#ifdef GRAPHICS_ASSETS
    if(g->area)return x>=0 && y>=0 && x<96 && y<96 && town_walkable(x,y);
#endif
    if(!(x>=0 && y>=0 && x<MAP_W && y<MAP_H && g->map[y][x]))return 0;
#ifdef GRAPHICS_ASSETS
    /* Every authoritative prop footprint is inside its anchor's 2x2 box.
     * Reject distant props before decoding their kind/state predicates. */
    for(int i=0;i<MAX_PROPS;i++){const Prop *p=&g->props[i];if((unsigned)((int)p->x-x)<=1u&&(unsigned)((int)p->y-y)<=1u&&game_prop_blocks(p,x,y))return 0;}
#endif
    return 1;
}
#ifdef GRAPHICS_ASSETS
/* Only the active region lives in RAM. Native region hooks commit before replacement. */
/* Class tag stays in the existing hero header; growth lives outside Game. */
static uint16_t hero_type;
unsigned game_hero_class(void){return (hero_type&0xff00)==0x4300&&(hero_type&255)<3?hero_type&255:0;}
int game_max_hp(void){return item_effects.max_hp?item_effects.max_hp:100;}
__attribute__((section(".text.ui"),noinline)) void game_create_hero(CharacterState *h,unsigned cls){
 static const uint8_t hp[3]={70,45,30},mana[3]={10,22,70};if(cls>2)cls=0;
 /* Native class weapon; requested release kit gives every class two heals.
  * CreatePlrItems stores starter potions in the first two belt slots. */
 *h=(CharacterState){.hp=hp[cls],.mana=mana[cls],.max_mana=mana[cls],.gold=500,.potions=2,.reserved=(uint16_t)(0x4300|cls)};
 game_progress_init(&h->progress,cls);
 items_init(&h->inventory,cls);
 unsigned heal=game_belt_code(ITEM_HEAL);h->belt=heal|((uint64_t)heal<<4);
}

void __attribute__((section(".text.ui"),noinline)) game_get_character(const Game *g,CharacterState *h){
    h->hp=g->hp;h->mana=g->mana;h->max_mana=g->max_mana;h->gold=g->gold;h->gold_reserved=0;h->potions=g->potions;h->reserved=hero_type;h->belt=g->belt;h->belt_reserved=0;h->progress=hero_progress;if(&h->inventory!=game_inventory())h->inventory=*game_inventory();
}
void __attribute__((section(".text.ui"),noinline)) game_apply_character(Game *g,const CharacterState *h){
    hero_type=h->reserved;hero_progress=h->progress;
    if(&h->inventory!=game_inventory()){*game_inventory()=h->inventory;game_items_recalc();}else game_items_select();
    g->hp=h->hp;g->mana=h->mana;g->max_mana=item_effects.max_mana;g->gold=h->gold;g->gold_reserved=0;g->potions=h->potions;g->belt=h->belt;if(item_effects.special_flags&ITEM_NOMANA){g->mana=0;hero_progress.mana_fraction=0;}
}
void __attribute__((section(".text.ui"),noinline)) game_clear_character(Game *g){
    g->hp=g->mana=g->max_mana=g->gold=g->potions=0;g->belt=0;
}
__attribute__((section(".text.ui"),noinline)) void game_new_campaign(Game *g,const CharacterState *hero){
    CharacterState retained=*hero;
    game_init(g);game_apply_character(g,&retained);
    if(g->hp<=0){g->hp=game_max_hp();g->mana=g->max_mana;hero_progress.mana_debt=hero_progress.mana_fraction=hero_progress.life_steal_fraction=0;}
    game_enter_town(g,1);
}
void game_enter_town(Game *g,int fresh){game_region_town(g,fresh);}
void game_enter_dungeon(Game *g){game_region_dungeon(g);}
/* Original town exits: catacombs 5, caves 9, hell 13. Bits13..15 share
 * the existing serialized quest flag word; no save structure grows. */
__attribute__((section(".text.ui"),target("thumb"),minsize)) unsigned game_shortcut_mask(void){return coop_active?7:(quest_progress.finale_flags&TOWN_WARP_FLAGS)>>13;}
__attribute__((section(".text.ui"),target("thumb"),minsize)) int game_shortcut_target(const Game *g){
 if(g->area){
  static const uint8_t xy[6]={49,21,17,69,41,80};unsigned mask=game_shortcut_mask();
  for(unsigned i=0;i<3;i++)if((mask&(1u<<i))&&abs_i(g->x-xy[i*2])<=2&&abs_i(g->y-xy[i*2+1])<=2)return 5+i*4;
 }else if(dungeon_level==5||dungeon_level==9||dungeon_level==13){
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int x=g->x+dx,y=g->y+dy;if((unsigned)x<80&&(unsigned)y<80&&g->map[y][x]==3)return (int)dungeon_level;}
 }
 return 0;
}
__attribute__((section(".text.ui"),target("thumb"),minsize)) void game_shortcut_spawn(Game *g){
 for(int y=0;y<80;y++)for(int x=0;x<80;x++)if(g->map[y][x]==3){
  for(int r=1;r<=4;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)if(game_walkable(g,x+dx,y+dy)){g->x=x+dx;g->y=y+dy;return;}
 }
 game_dungeon_spawn(g);
}


#endif
#ifdef GRAPHICS_ASSETS
/* The full population persists, but only8 nearby monsters can be active.
 * Dormant records neither collide nor attack: never create invisible attackers
 * when a player brings more monsters than the hardware sprite cache can show. */
/* Runs every tick from ROM over all 112 actors: Thumb halves its opcode fetches. */
void __attribute__((section(".text.ui"),noinline,target("thumb"))) game_activate_nearby(Game *g){
    coop_enemy_invalidate();
    /* A max heap keeps the worst selected candidate at the root. Packing the
     * stable actor index after the distance preserves the original lowest-ID
     * tie break without sorting/shifting the whole shortlist for each actor.
     * The heap is temporary; AI still visits actors in its original order. */
    uint32_t nearest[12];unsigned count=0,limit=coop_active?4:8;
    for(unsigned i=0;i<MAX_ENEMIES;i++){
        if(!g->enemies[i].alive){g->enemy_attack[i]=0;continue;}
        g->enemies[i].alive=2;
        int dx=g->enemies[i].x-g->x,dy=g->enemies[i].y-g->y;
        int distance=coop_active?coop_distance(g->enemies[i].x,g->enemies[i].y):dx*dx+dy*dy;
        if(distance>=20000){g->enemy_attack[i]=0;continue;}
        uint32_t key=((uint32_t)distance<<8)|i;unsigned at;
        if(count<limit){
            at=count++;
            while(at){unsigned parent=(at-1)>>1;if(nearest[parent]>=key)break;nearest[at]=nearest[parent];at=parent;}
        }else{
            if(key>=nearest[0]){g->enemy_attack[i]=0;continue;}
            /* The evicted candidate cannot be selected again in this pass. */
            g->enemy_attack[nearest[0]&255]=0;
            at=0;
            for(;;){
                unsigned child=at*2+1;if(child>=count)break;
                if(child+1<count&&nearest[child+1]>nearest[child])child++;
                if(nearest[child]<=key)break;
                nearest[at]=nearest[child];at=child;
            }
        }
        nearest[at]=key;
    }
    for(unsigned i=0;i<count;i++)g->enemies[nearest[i]&255].alive=1;
    /* Rejected/evicted and dead actors were reset in the same traversal.
     * Selected actors retain their exact counter without a second full scan. */
}
#endif
#ifndef GRAPHICS_ASSETS
static int occupied(const Game *g,int x,int y,int except) {
    for(int i=0;i<MAX_ENEMIES;i++) if(i!=except && g->enemies[i].alive==1 &&
        g->enemies[i].x==x && g->enemies[i].y==y) return 1;
    return 0;
}
#endif
void __attribute__((section(".text.ui"),noinline)) game_init(Game *g) {
    uint8_t *p=(uint8_t*)g;
    for(unsigned i=0;i<sizeof(*g);i++) p[i]=0;
#ifdef GRAPHICS_ASSETS
    game_spell_new_campaign();hero_type=0;items_init(&hero_inventory,0);game_progress_init(&hero_progress,0);game_items_recalc();game_arrows_reset();game_ai_reset();game_explore_reset();
    g->target=-1;g->swing_target=-1;
    g->mana=100;g->max_mana=100;
    room(g,2,2,12,12);
    g->map[4][6]=0;g->map[8][10]=0;
#else
    room(g,2,2,8,8); room(g,14,2,8,8);
    room(g,2,14,8,8); room(g,14,14,8,8);
    room(g,9,5,6,3); room(g,5,9,3,6);
    room(g,17,9,3,6); room(g,9,17,6,3);
    g->map[4][7]=0; g->map[7][4]=0;
    g->map[16][16]=0; g->map[19][19]=0;
#endif
    g->x=4; g->y=4; g->hp=100; g->potions=3;
#ifdef GRAPHICS_ASSETS
    g->belt=1u|(1u<<4)|(1u<<8);
#endif
    static const uint8_t positions[12][2]={
#ifdef GRAPHICS_ASSETS
       {7,6},{8,8},{11,5},{12,3},{12,9},{6,11},
       {3,12},{8,12},{11,11},{12,12},{10,3},{3,10}};
#else
       {7,6},{8,8},{15,5},{19,4},{20,8},{6,15},
       {3,19},{8,20},{15,17},{20,16},{17,20},{20,20}};
#endif
    for(int i=0;i<12;i++) {
        g->enemies[i]=(Enemy){positions[i][0],positions[i][1],30,1,(uint8_t)(i&1),0};
    }
#ifdef GRAPHICS_ASSETS
    game_generate_next(g);
#else
    g->map[20][20]=2;
#endif
}
#ifndef GRAPHICS_ASSETS
static void drop_at(Game *g,int x,int y,int kind) {
    for(int i=0;i<MAX_DROPS;i++) if(!g->drops[i].active) {
        g->drops[i]=(Drop){.x=x,.y=y,.active=1,.kind=(uint8_t)kind}; return;
    }
}
#endif
#ifdef GRAPHICS_ASSETS
uint32_t monster_history[MONSTER_HISTORY_TYPES];
volatile uint32_t hazard_audio_event;
void __attribute__((section(".text.ui"),target("thumb"),minsize)) game_hazard_sound(unsigned type){
 if(type<3)hazard_audio_event=((hazard_audio_event+256)&0xffffff00u)|type;
}
#include "../generated/monster_defs.inc"
#include "monster_unique.inc"
unsigned __attribute__((section(".text.ui"),target("thumb"),minsize)) game_monster_variant(const Game *g,unsigned family){
 unsigned result=family;
 if(dungeon_level<monster_defs[result].min_floor||dungeon_level>monster_defs[result].max_floor){for(unsigned k=0;k<MONSTER_TYPES;k++)if(monster71_normal[k]&&monster_defs[k].family==family&&dungeon_level>=monster_defs[k].min_floor&&dungeon_level<=monster_defs[k].max_floor){result=k;break;}}
 for(unsigned i=0;i<MAX_ENEMIES;i++){unsigned k=game_monster_base_kind(g->enemies[i].kind);if(monster71_normal[k]&&monster_defs[k].family==family&&k>result)result=k;}
 return result;
}
#include "difficulty.inc"
unsigned __attribute__((section(".text.ui"),target("thumb"),minsize)) game_monster_attack_ticks(unsigned kind){return monster_defs[kind].attack_frames;}
static void __attribute__((section(".text.ui"),noinline)) damage_enemy(Game *g,int hit,int damage,int credit){
        if(hit<0||hit>=MAX_ENEMIES||!g->enemies[hit].alive||damage<=0)return;
        if(game_finale_protected(g->enemies[hit].kind)||!game_monster_damageable(g,hit))return;
        if(credit)game_credit_hit(g,hit);Enemy *e=&g->enemies[hit];if(!e->max_hp&&e->hp>0)e->max_hp=(e->kind==MONSTER_ADVOCATE||e->kind==MONSTER_LAZARUS||e->kind==MONSTER_ZHAR)?game_monster_hp_bound(e->kind,1):(unsigned)e->hp;e->hp-=damage;g->flash=7;
        if(e->hp>0)game_monster_damage_reaction(g,hit,(unsigned)damage);
        if(e->hp<=0){if(e->kind<MONSTER_TYPES&&monster_history[game_monster_history_index(e->kind)]!=0xffffffffu)monster_history[game_monster_history_index(e->kind)]++;game_credit_kill(g,hit);game_corpse_add(g,hit);if(e->kind==MONSTER_BUTCHER){butcher_quest=3;g->town_message=251;g->pickup_notice_ticks=100;}e->alive=0;game_fallen_fear(g,e->x,e->y);g->kills++;game_item_monster_drop(g,hit);}
}
void __attribute__((section(".text.ui"),target("thumb"),minsize)) game_damage_enemy(Game *g,int hit,int damage){damage_enemy(g,hit,damage,1);}
static void __attribute__((section(".text.ui"),target("thumb"),minsize)) damage_enemy_element(Game *g,int hit,int damage,unsigned element,int credit){
 if(hit<0||hit>=MAX_ENEMIES||element>3)return;
 unsigned kind=g->enemies[hit].kind;
 if(element&&(game_monster_immunities(kind)&(1u<<(element-1))))return;
 if(element&&(game_monster_resistances(kind)&(1u<<(element-1))))damage/=4;
 damage_enemy(g,hit,damage,credit);
}
void __attribute__((section(".text.ui"),target("thumb"),minsize)) game_damage_enemy_element(Game *g,int hit,int damage,unsigned element){damage_enemy_element(g,hit,damage,element,1);}
void __attribute__((section(".text.ui"),target("thumb"),minsize)) game_damage_enemy_environment(Game *g,int hit,int damage,unsigned element){damage_enemy_element(g,hit,damage,element,0);}

#endif
#ifdef GRAPHICS_ASSETS
/* Collect adjacent gold while approaching it. A stationary manual gold drop
 * stays on the ground until the hero moves again. Both diagonal corridors
 * must be open, so pickup never reaches through a wall or a closed door. */
static __attribute__((noinline)) void game_auto_gold(Game *g){
 if(coop_active&&(coop_cpu_mask&(1u<<coop_turn)))return;
 for(int i=0;i<MAX_DROPS;i++){
  Drop *d=&g->drops[i];if(!d->active||d->kind!=ITEM_GOLD)continue;
  int dx=(int)d->x-g->x,dy=(int)d->y-g->y;
  if(dx<-1||dx>1||dy<-1||dy>1)continue;
  if((dx||dy)&&!g->move_wait)continue;
  if(!game_walkable(g,d->x,d->y))continue;
  if(dx&&dy&&(!game_walkable(g,d->x,g->y)||!game_walkable(g,g->x,d->y)))continue;
  game_item_pickup(g,i);
 }
}
#endif
#ifdef GRAPHICS_ASSETS
#include "region_input.inc"
#endif
void game_step(Game *g,unsigned held,unsigned pressed) {
    if(pressed&KEY_START) { game_init(g);
#ifdef GRAPHICS_ASSETS
        game_enter_town(g,1);
#endif
        return; }
#ifndef GRAPHICS_ASSETS
    if(pressed&KEY_SELECT) g->paused^=1;
#endif
    if(g->paused || g->won) return;
#ifdef GRAPHICS_ASSETS
    /* Diablo's completed kill must finish even after a simultaneous hero
     * death. Other dead-player world simulation remains stopped. */
    if(!coop_active&&!g->area&&dungeon_level==16&&quest_progress.state[5]==3){g->ticks++;game_finale_step(g);return;}
#endif
    if(g->hp<=0) return;
    #ifdef GRAPHICS_ASSETS
    if(!coop_active)g->ticks++;
    #else
    g->ticks++;
    #endif
#ifdef GRAPHICS_ASSETS
    if(!coop_active){if(game_finale_step(g))return;game_quest_npc_step(g);}
    game_item_effects_step(g);if(g->hp<=0)return;
    if(hero_progress.infra_ticks)hero_progress.infra_ticks--;
    if(!coop_active){game_quest_enter(g);game_props_step(g);}
#endif
#ifdef GRAPHICS_ASSETS
    if(g->pickup_notice_ticks)g->pickup_notice_ticks--;
#endif
    if(g->move_wait) g->move_wait--;
    if(g->attack_wait) g->attack_wait--;
    if(g->flash) g->flash--;
    if(g->hit_flash){g->hit_flash--;
#ifdef GRAPHICS_ASSETS
      if(!g->hit_flash&&g->reaction_duration){game_wear_equipment(g,g->reaction_block?WEAR_BLOCK:WEAR_HIT);g->reaction_duration=g->reaction_block=0;}
#endif
    }
#ifdef GRAPHICS_ASSETS
    if(pressed&KEY_L)game_use_potion(g,1);
    if(pressed&KEY_R)game_use_potion(g,2);
#else
    if((pressed&KEY_B) && g->potions && g->hp<100){g->potions--;g->hp+=40;if(g->hp>100)g->hp=100;}
#endif
#ifdef GRAPHICS_ASSETS
    if((pressed&KEY_A)&&game_try_travel(g))return;
    if(g->area){
        game_controller_step(g,held&~KEY_A,pressed&~KEY_A);
        int item=game_item_target(g);g->target=game_spell_portal_target(g)>=0?TARGET_PORTAL:item>=0?TARGET_ITEM:-1;
        if(item>=0&&!g->move_wait&&(pressed&KEY_A))game_item_pickup(g,item);
        game_auto_gold(g);return;
    }
#endif
#ifdef GRAPHICS_ASSETS
    if(!coop_active)game_activate_nearby(g);
    int hit=game_controller_step(g,held,pressed);
    if(hit>=0){
        if(game_melee_hit(g,(unsigned)hit))game_melee_damage(g,hit,game_weapon_damage(g));
    }    else if(hit<=-2&&hit>=-65){unsigned barrel=game_prop_is_barrel(g->props[-hit-2].kind);if(game_open_prop(g,-hit-2)&&barrel)game_wear_equipment(g,WEAR_MELEE);}

#else
    if(!g->move_wait) {
        int sx=((held&KEY_RIGHT)!=0)-((held&KEY_LEFT)!=0);
        int sy=((held&KEY_DOWN)!=0)-((held&KEY_UP)!=0);
        int dx=sign(sx+sy),dy=sign(sy-sx);
        if(dx||dy) {
            int x=g->x+dx,y=g->y+dy;
            int clear=game_walkable(g,x,y) && !occupied(g,x,y,-1);
            if(dx && dy) clear=clear && game_walkable(g,g->x+dx,g->y)
                    && game_walkable(g,g->x,g->y+dy);
            if(clear) { g->x=x; g->y=y; }
            g->move_wait=9;
        }
    }
    if((held&KEY_A) && !g->attack_wait) {
        g->attack_wait=18; g->flash=7;
        for(int i=0;i<MAX_ENEMIES;i++) {
            Enemy *e=&g->enemies[i];
            if(e->alive && abs_i(e->x-g->x)<=1 && abs_i(e->y-g->y)<=1) {
                e->hp-=15;
                if(e->hp<=0) { e->alive=0; g->kills++; drop_at(g,e->x,e->y,g->kills%2==0?1+(g->kills/2-1)%3:0); }
                break;
            }
        }
    }
#endif
#ifdef GRAPHICS_ASSETS
    if(!coop_active){game_arrows_step(g);game_monsters_step(g);}
#else
    if(g->ticks%30==0) {
        for(int i=0;i<MAX_ENEMIES;i++) {
            Enemy *e=&g->enemies[i]; if(e->alive!=1) continue;
            int dx=g->x-e->x,dy=g->y-e->y;
            if(abs_i(dx)>7 || abs_i(dy)>7) continue;
            if(abs_i(dx)<=1 && abs_i(dy)<=1) {
                g->hp-=3; if(g->hp<0) g->hp=0; g->hit_flash=6; continue;
            }
            int nx=e->x,ny=e->y;
            if(abs_i(dx)>abs_i(dy)) nx+=sign(dx); else ny+=sign(dy);
            if(!game_walkable(g,nx,ny) || occupied(g,nx,ny,i)) {
                nx=e->x; ny=e->y;
                if(abs_i(dx)>abs_i(dy)) ny+=sign(dy); else nx+=sign(dx);
            }
            if(game_walkable(g,nx,ny) && !occupied(g,nx,ny,i) && !(nx==g->x && ny==g->y)) {
                e->x=nx; e->y=ny;
            }
        }
    }
#endif
#ifdef GRAPHICS_ASSETS
    game_auto_gold(g);
#else
    for(int i=0;i<MAX_DROPS;i++) {
        Drop *d=&g->drops[i];
        if(d->active&&d->x==g->x&&d->y==g->y){if(d->kind)g->potions++;else g->gold+=10;d->active=0;}
    }
#endif
#ifndef GRAPHICS_ASSETS
    if(g->map[g->y][g->x]==2 && g->kills==MAX_ENEMIES) g->won=1;
#endif
}
