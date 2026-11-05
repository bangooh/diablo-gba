/* Retail MAI_RoundRanged / MAI_RR2 / MAI_Garg / MAI_Scav adapted to the
 * bounded GBA actor clocks. No additional per-actor allocation or full map scan. */
static int completion_ranged(unsigned kind){unsigned a=game_monster_ai(kind);return a==MONSTER_AI_ACID||a==MONSTER_AI_ACIDUNIQ||a==MONSTER_AI_MAGMA||a==MONSTER_AI_STORM||a==MONSTER_AI_MEGA;}
static unsigned completion_spell(unsigned kind){unsigned a=game_monster_ai(kind);return a==MONSTER_AI_STORM?SPELL_LIGHTNING:a==MONSTER_AI_MAGMA?MONSTER_MISSILE_MAGMA:a==MONSTER_AI_MEGA?MONSTER_MISSILE_FLAME:MONSTER_MISSILE_ACID;}
static void completion_shoot(Game *g,int i){unsigned n=monster71_special_frames[g->enemies[i].kind];special[i]=n?n:8;delayed[i]=3;g->enemy_facing[i]=facing(g->x-g->enemies[i].x,g->y-g->enemies[i].y);}
static AI_ROM void completion_ranged_think(Game *g,int i,int sight,int adjacent,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];if(!seen[i])return;
 unsigned a=game_monster_ai(e->kind),distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy),roll=random_n(10000),less=a==MONSTER_AI_ACID;
 int dir=facing(last_x[i]-e->x,last_y[i]-e->y);g->enemy_facing[i]=dir;
 if(a==MONSTER_AI_ACIDUNIQ){if(sight&&distance<4&&random_n(100)<10*(intelligence+7) ){call_walk(g,i,(dir+4)&7,0);if(walk[i])return;}if(sight)completion_shoot(g,i);else call_walk(g,i,dir,0);return;}
 if(distance>=2&&sight&&monster_same_room(g,e)&&(retreat[i]||((distance>=3)&&(a==MONSTER_AI_MEGA||!random_n(4u<<less))))){
  if(!retreat[i]){fraction[i]=0;away[i]=random_n(2);}retreat[i]=1;
  if(fraction[i]++<2*distance||!can_step(g,i,dir)){
   if(a!=MONSTER_AI_MEGA&&roll<(500*(intelligence+1)>>less))completion_shoot(g,i);
   else king_round_walk(g,i,dir);return;
  }
 }
 retreat[i]=0;
 if(sight&&((distance>=3&&roll<(500*(intelligence+2)>>less))||roll<(500*(intelligence+1)>>less)||(a==MONSTER_AI_MEGA&&distance<=4&&!random_n(2))))completion_shoot(g,i);
 else if(distance>=2)call_walk(g,i,dir,0);
 else if(adjacent&&roll<1000*(intelligence+6))g->enemy_attack[i]=game_monster_attack_ticks(e->kind);
 if(!walk[i]&&!special[i]&&!g->enemy_attack[i])wait[i]=5+random_n(10);
}
static AI_ROM int completion_gargoyle(Game *g,int i,int sight,int dx,int dy,unsigned intelligence){
 Enemy *e=&g->enemies[i];unsigned distance=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);
 (void)sight; /* Sleep, wake and healing advance in gargoyle_step at20Hz. */
 if(e->hp<(int)e->max_hp/2)retreat[i]=1;
 if(retreat[i]){
  if(distance>=intelligence+2){rally[i]=GARG_HEAL;retreat[i]=fraction[i]=0;away[i]=4+random_n(5);special[i]=monster71_special_frames[e->kind]*gargoyle_rate(e->kind);return 1;}
  call_walk(g,i,(facing(dx,dy)+4)&7,0);if(walk[i])return 1;retreat[i]=0;
 }
 return 0;
}
static AI_ROM int completion_scavenger(Game *g,int i){
 Enemy *e=&g->enemies[i];if(e->hp>=(int)e->max_hp*3/4){retreat[i]=0;return 0;}
 if(e->hp<(int)e->max_hp/2&&!retreat[i]){retreat[i]=10;game_pack_forget(i);}
 if(!retreat[i])return 0;retreat[i]--;
 unsigned first=random_n(2)?0:MAX_ENEMIES-1;
 for(unsigned n=0;n<MAX_ENEMIES;n++){
  const Corpse *c=&game_corpses[first?MAX_ENEMIES-1-n:n];if(!c->ticks||c->ticks>CORPSE_TICKS)continue;
  int dx=(int)c->x-e->x,dy=(int)c->y-e->y;if(abs_i(dx)>4||abs_i(dy)>4)continue;
  if(!dx&&!dy){e->hp++;special[i]=monster71_special_frames[e->kind];return 1;}
  call_walk(g,i,facing(dx,dy),0);if(walk[i])return 1;
 }
 return 0;
}
