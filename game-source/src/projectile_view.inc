/* Presentation only. One shared 0..2 hardware-frame phase replaces a second
 * eight-projectile position array. Never write the simulation's Arrow state.
 * phase0 is the completed tick; phase1/2 blend its previous/current endpoints.
 * Network playback keeps its existing authoritative coordinates. */
static unsigned projectile_phase;
#ifndef PROJECTILE_INTERPOLATION
#define PROJECTILE_INTERPOLATION 1
#endif
static UI_FN void projectile_world(const Arrow *a,unsigned spell,int *wx,int *wy){
 int x=a->x,y=a->y;
 if(PROJECTILE_INTERPOLATION&&projectile_phase&&!coop_active&&!game.paused&&
    (a->vx||a->vy)&&
    (a->direction&(HOSTILE_PROJECTILE|HOSTILE_BURST))!=(HOSTILE_PROJECTILE|HOSTILE_BURST)&&
    spell!=SPELL_FIREWALL&&spell!=SPELL_INFERNO&&spell!=SPELL_GUARDIAN&&
    spell!=SPELL_GOLEM&&spell!=SPELL_STONE&&spell!=SPELL_FLASH&&spell!=SPELL_APOCALYPSE&&
    (x!=(int)a->start_x*256||y!=(int)a->start_y*256)){
  /* Backward interpolation cannot pass the collision-tested current position.
   * Homing/charged projectiles retain the velocity used for their latest step;
   * a new unstepped projectile stays at its source instead of moving behind it. */
  unsigned remaining=3-projectile_phase;
  x-=(int)a->vx*(int)remaining/3;y-=(int)a->vy*(int)remaining/3;
 }
 *wx=DUNGEON_ORIGIN+((x-y)*16)/256;*wy=72+((x+y)*8)/256;
}
