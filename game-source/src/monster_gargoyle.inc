/* Retail InitMonster / M_StartHeal / M_DoHeal: Special frame zero is the
 * folded statue. Reverse into it, heal there, then play forward to wake.
 * Existing, hashed clocks hold all state; no extra per-monster RAM is used.
 * special is a 20 Hz timer, fraction is a Q6 HP remainder, and away is the
 * randomly chosen healing divisor (4..8). These fields are family-specific. */
enum { GARG_SLEEP=0, GARG_AWAKE=1, GARG_HEAL=2 };
static unsigned gargoyle_rate(unsigned kind){
 /* monstdat Rate[MA_SPECIAL]: Winged-Demon/Gargoyle 2, Blood Claw/Death Wing 0
  * (zero advances every tick). Unique actors inherit their base animation. */
 return game_monster_base_kind(kind)<94?2:1;
}
static void gargoyle_wake(Game *g,int i){
 rally[i]=GARG_AWAKE;retreat[i]=wait[i]=delayed[i]=fraction[i]=0;
 special[i]=monster71_special_frames[g->enemies[i].kind]*gargoyle_rate(g->enemies[i].kind);
}
static AI_ROM int gargoyle_lift(Game *g,int i){
 if(game_monster_ai(g->enemies[i].kind)!=MONSTER_AI_GARG||
    (rally[i]!=GARG_SLEEP&&(rally[i]!=GARG_HEAL||special[i])))return 0;
 gargoyle_wake(g,i);return 1;
}
/* Called before generic golem/attack dispatch, even on non-decision ticks.
 * Stone Curse returns before this function, freezing both pose and HP. */
static AI_ROM int gargoyle_step(Game *g,int i){
 Enemy *e=&g->enemies[i];
 if(rally[i]==GARG_SLEEP){
  int radius=monster_defs[e->kind].intelligence+2;
  if(seen[i]&&abs_i(g->x-e->x)<radius&&abs_i(g->y-e->y)<radius)gargoyle_wake(g,i);
  return 1;
 }
 if(rally[i]==GARG_HEAL){
  if(special[i]>gargoyle_rate(e->kind)){--special[i];return 1;}
  special[i]=0;
  /* Original healing is floor(maxHP_Q6/(16*(random(5)+4))) per tick.
   * Keep sub-HP progress so low-HP gargoyles do not heal too quickly. */
  unsigned value=fraction[i]+((unsigned)e->max_hp*4)/away[i];
  e->hp+=value>>6;fraction[i]=value&63;
  if(e->hp>=e->max_hp){e->hp=e->max_hp;gargoyle_wake(g,i);}
  return 1;
 }
 if(special[i]){--special[i];return 1;}
 return 0;
}
