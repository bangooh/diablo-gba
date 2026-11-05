#include "game.h"
#include "coop.h"
/* Devilution player.cpp: ExpLvlsTbl, NextPlrLevel, ModifyPlr*,
 * AddPlrExperience/AddPlrMonstExper. No floating point on ARM7. */
HeroProgress hero_progress;
uint8_t monster_credit[MAX_ENEMIES];
static const uint32_t experience[51]={0,2000,4620,8040,12489,18258,25712,35309,47622,63364,83419,108879,141086,181683,231075,313656,424067,571190,766569,1025154,1366227,1814568,2401895,3168651,4166200,5459523,7130496,9281874,12042092,15571031,20066900,25774405,32994399,42095202,53525811,67831218,85670061,107834823,135274799,169122009,210720231,261657253,323800420,399335440,490808349,601170414,733825617,892680222,1082908612,1310707109,1583495809};
static const uint8_t initial[3][4]={{30,10,20,25},{20,15,30,20},{15,35,15,20}};
static const uint8_t limits[3][4]={{250,50,60,100},{55,70,250,80},{45,250,85,80}};
uint32_t game_next_experience(unsigned level){return experience[level<50?level:49];}
unsigned game_stat_limit(unsigned cls,unsigned stat){return limits[cls<3?cls:0][stat<4?stat:0];}
void game_progress_init(HeroProgress *p,unsigned cls){
 if(cls>2)cls=0;
 *p=(HeroProgress){0};p->level=1;p->spell_slot[0]=SPELL_SKILL;p->spell_selected=SPELL_SKILL;if(cls==2)p->spell_level[0]=2;
 p->strength=initial[cls][0];p->magic=initial[cls][1];p->dexterity=initial[cls][2];p->vitality=initial[cls][3];
 p->max_hp_q6=(cls==0?70:cls==1?45:30)*64;
 p->max_mana_q6=cls==0?640:cls==1?1440:4480;
}
int game_progress_valid(const CharacterState *h){
 const HeroProgress *p=&h->progress;unsigned cls=h->reserved&255;
 if(h->gold<0||h->gold>GAME_GOLD_MAX||h->gold_reserved||p->infra_ticks>10000||p->completion>3)return 0;
 if(p->life_steal_fraction>63||p->life_drain_fraction>15||p->mana_debt>32767||(p->mana_debt&&h->mana)||h->belt_reserved||!game_belt_valid(h->belt,h->potions))return 0;
 if(p->spell_level[0]>15||p->spell_level[1]>15||p->mana_fraction>63||!game_spell_choice_valid(p->spell_selected))return 0;
 for(unsigned i=1;i<=SPELL_MAX;i++)if(game_spell_level(p,i)&&!game_spell_supported(i))return 0;
 for(unsigned i=0;i<4;i++)if(!game_spell_choice_valid(p->spell_slot[i]))return 0;
 if(cls>2||p->level<1||p->level>50||p->experience>2000000000u||p->points>245||p->spell_active_slot>3)return 0;
 if(p->experience<experience[p->level-1]||(p->level<50&&p->experience>=experience[p->level]))return 0;
 const uint8_t *a=&p->strength;
 /* Retail shrines can lower base attributes below their starting values. */
 for(unsigned i=0;i<4;i++)if(a[i]>limits[cls][i])return 0;
 ItemEffects effects;item_build_effects(p,&h->inventory,cls,&effects);
 return p->max_hp_q6>=64&&p->max_hp_q6<64000&&p->max_mana_q6<64000&&h->hp<=effects.max_hp&&h->max_mana==effects.max_mana&&h->mana>=0&&h->mana<=h->max_mana;
}
static unsigned remaining(const HeroProgress *p,unsigned cls){
 return limits[cls][0]-p->strength+limits[cls][1]-p->magic+limits[cls][2]-p->dexterity+limits[cls][3]-p->vitality;
}
static void gain(CharacterState *h,unsigned ml,unsigned exp){
 HeroProgress *p=&h->progress;unsigned cls=h->reserved&255;
 if(h->hp<=0||cls>2||p->level<1||p->level>50)return;
 int scale=10+(int)ml-p->level;
 if(scale<=0)exp=0;
 else{unsigned whole=exp/10,part=exp%10*(unsigned)scale/10;
  exp=whole>(2000000000u-part)/(unsigned)scale?2000000000u:whole*(unsigned)scale+part;
 }
 if(coop_active){unsigned cap=experience[p->level]/20;if(exp>cap)exp=cap;cap=200*p->level;if(exp>cap)exp=cap;}
 if(exp>2000000000u-p->experience)p->experience=2000000000u;else p->experience+=exp;
 /* Apply the final level's rewards too, rather than retail's early-return bug. */
 while(p->level<50&&p->experience>=experience[p->level]){
  p->level++;unsigned available=remaining(p,cls),points=p->points+5;
  p->points=points<available?points:available;
  p->max_hp_q6+=(cls==2?64:128)+(coop_active?0:1);
  p->max_mana_q6+=(cls==0?64:128)+(coop_active?0:1);
  ItemEffects effects;item_build_effects(p,&h->inventory,cls,&effects);p->mana_debt=0;p->mana_fraction=p->life_steal_fraction=0;h->hp=effects.max_hp;h->max_mana=effects.max_mana;h->mana=(effects.special_flags&ITEM_NOMANA)?0:effects.max_mana;
 }
}
void game_gain_experience(Game *g,unsigned ml,unsigned exp){
 CharacterState h;game_get_character(g,&h);unsigned level=h.progress.level;
 gain(&h,ml,exp);game_apply_character(g,&h);
 if(h.progress.level!=level){g->pickup_notice=6;g->pickup_notice_ticks=100;}
}
int game_spend_stat(Game *g,unsigned stat){
 HeroProgress *p=&hero_progress;unsigned cls=game_hero_class();
 if(stat>3||!p->points||g->hp<=0)return 0;
 uint8_t *value=&p->strength+stat;if(*value>=limits[cls][stat])return 0;
 ++*value;--p->points;
 if(stat==1){unsigned delta=cls==2?128:64;p->max_mana_q6+=delta;}
 if(stat==3){unsigned delta=cls==0?128:64;p->max_hp_q6+=delta;}
 game_items_update(g);return 1;
}
void game_credit_hit(Game *g,int i){(void)g;monster_credit[i]|=1u<<(coop_active?coop_turn:0);}
void game_credit_kill(Game *g,int i){
 /* Use the final InitMonster/PlaceUniqueMonst level and XP before sharing. */
 unsigned kind=g->enemies[i].kind;if(kind>=MONSTER_TYPES)return;
 unsigned mask=monster_credit[i]&(coop_active?coop_members:1),n=0;
 for(unsigned j=0;j<4;j++)n+=!!(mask&(1u<<j));if(!n)return;
 unsigned level=game_monster_level(kind),xp=game_monster_xp(kind)/n;
 for(unsigned j=0;j<4;j++)if(mask&(1u<<j)){
  if(!coop_active||j==coop_turn)game_gain_experience(g,level,xp);
  else{CoopPlayer *p=&coop_players[j];unsigned previous=p->hero.progress.level;gain(&p->hero,level,xp);if(previous!=p->hero.progress.level){p->notice=6;p->notice_ticks=100;}}
 }
 monster_credit[i]=0;
}
unsigned game_attribute_damage(const HeroProgress *p,const ItemEffects *e,unsigned cls){
 return cls==1?p->level*(e->stats[0]+e->stats[2])/200:p->level*e->stats[0]/((e->weapon_flags&ITEM_BOW)?200:100);
}
unsigned game_weapon_damage(const Game *g){
 unsigned bonus=game_attribute_damage(&hero_progress,&item_effects,game_hero_class());
 uint32_t roll=(g->ticks+1u)*2654435761u+(coop_active?coop_turn:0)*2246822519u;
 roll^=roll>>16;
 return item_weapon_roll(roll,bonus);
}
unsigned game_bow_accuracy(unsigned owner){
 const HeroProgress *p=coop_active?&coop_players[owner].hero.progress:&hero_progress;
 const Inventory *bag=coop_active?&coop_players[owner].hero.inventory:game_inventory();unsigned cls=coop_active?(coop_players[owner].hero.reserved&255):game_hero_class();
 ItemEffects effects;item_build_effects(p,bag,cls,&effects);int chance=p->level+effects.stats[2]+50+(cls==0?10:cls==1?20:0)+effects.to_hit+effects.armor_pierce;return chance>0?(unsigned)chance:0;
}
