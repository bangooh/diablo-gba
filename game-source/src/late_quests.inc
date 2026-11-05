#include "../generated/late_quests.h"
static QUEST_FN void late_spawn(Game *g){
 if(!game_campaign_layout()||g->area||(dungeon_level!=6&&dungeon_level!=13))return;
 unsigned q=dungeon_level==6?14:11;if(!quest_progress.state[q])return;
 unsigned book=q==14?PROP_BONE_BOOK:PROP_WARLORD_BOOK;
 for(unsigned i=0;i<MAX_PROPS;i++)if(g->props[i].kind==book)return;
 int x,y;if(!quest_place(g,0x424f4e45u+q,1,&x,&y))return;
 for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){g->props[i]=(Prop){x,y,book,!!quest_progress.stage[q]};break;}
 int ox=quest_valor_cell%80,oy=quest_valor_cell/80;
 if(q==14){for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){g->props[i]=(Prop){ox+6,oy+7,PROP_BONE_ENTRY,0};break;}}
 else for(unsigned n=0;n<sizeof(late_warlord_objects)/3;n++)for(unsigned i=0;i<MAX_PROPS;i++)if(!g->props[i].kind){const uint8_t *v=late_warlord_objects[n];g->props[i]=(Prop){ox+v[0],oy+v[1],v[2],0};break;}
}
QUEST_FN void game_late_population(Game *g){
 unsigned n=0;
 if(dungeon_level==REGION_BONE){
  for(unsigned i=0;i<MAX_ENEMIES;i++){g->enemies[i]=(Enemy){0};g->enemy_attack[i]=g->enemy_facing[i]=0;}
  for(unsigned i=0;i<MAX_PROPS;i++)g->props[i]=(Prop){0};for(unsigned i=0;i<MAX_DROPS;i++)g->drops[i]=(Drop){0};
  for(unsigned i=0;i<sizeof(late_bonecha1_guards)/3;i++){const uint8_t *v=late_bonecha1_guards[i];g->enemies[n++]=(Enemy){v[0],v[1],game_monster_hp(v[2],item_seed(campaign_seed^i)),1,v[2],0};}
  for(unsigned i=0;i<sizeof(late_bonecha1_objects)/3;i++){const uint8_t *v=late_bonecha1_objects[i];unsigned state=v[2]==PROP_BONE_TOME?quest_progress.state[14]==3:v[2]==PROP_BONE_LEVER?!!(quest_progress.bone_gates&(v[1]<20?1:2)):0;g->props[i]=(Prop){v[0],v[1],v[2],state};}
 }else if(dungeon_level==13){
  int ox=quest_valor_cell%80,oy=quest_valor_cell/80;
  for(unsigned i=0;i<sizeof(late_warlord_guards)/3;i++){const uint8_t *v=late_warlord_guards[i];g->enemies[n++]=(Enemy){ox+v[0],oy+v[1],game_monster_hp(v[2],item_seed(campaign_seed^i)),1,v[2],0};}
  g->enemies[MAX_ENEMIES-2]=(Enemy){ox+6,oy+7,game_monster_hp(MONSTER_WARLORD,campaign_seed),quest_progress.state[11]==3?0:1,MONSTER_WARLORD,0};
 }
}
static QUEST_FN int late_rack(Game *g,Prop *p){
 int slot=-1;for(unsigned i=0;i<MAX_DROPS;i++)if(!g->drops[i].active){slot=i;break;}if(slot<0)return 0;
 uint32_t seed=item_seed(campaign_seed^0x57415252u^((p->x+p->y*80)*0x4751u));int type=p->kind==PROP_WAR_ARMOR?9:(int)(1+seed%4);
 uint8_t choices[ITEM_COUNT];unsigned count=0;
 for(unsigned i=1;i<ITEM_COUNT;i++){const BaseItemDef *b=item_base(i);if(!item_defs[i].weight||b->min_level>26)continue;if(b->type==type)choices[count++]=i;}
 if(!count)return 0;Item it=item_create_level(choices[seed%count],seed,26);item_generate_magic(&it,26,1);
 for(int r=1;r<=3;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)if(game_item_space(g,p->x+dx,p->y+dy)){game_drop_put(g,item_to_drop(&it,p->x+dx,p->y+dy));p->state=1;game_item_sound(g,item_sound_kind(&it),0,p->x+dx,p->y+dy);return 1;}
 return 0;
}
QUEST_FN int game_late_use(Game *g,unsigned index){
 if(!game_campaign_layout()||g->area||index>=MAX_PROPS)return 0;Prop *p=&g->props[index];
 if(p->kind==PROP_BONE_ENTRY)return quest_progress.stage[14]?game_region_floor(g,REGION_BONE):0;
 if(p->state)return 0;
 if(p->kind==PROP_BONE_BOOK||p->kind==PROP_WARLORD_BOOK){unsigned q=p->kind==PROP_BONE_BOOK?14:11;quest_progress.state[q]=2;quest_progress.stage[q]=1;p->state=1;game_late_change(g,0,0);g->town_message=q==14?38:39;return 1;}
 if(p->kind==PROP_BONE_LEVER){quest_progress.bone_gates|=p->y<20?1:2;p->state=1;game_late_change(g,0,0);return 1;}
 if(p->kind==PROP_BONE_TOME){game_spell_grant(SPELL_GUARDIAN);quest_progress.state[14]=3;quest_progress.stage[14]=2;p->state=1;audio_quest_complete();g->town_message=40;return 1;}
 if(p->kind==PROP_WAR_ARMOR||p->kind==PROP_WAR_WEAPON)return late_rack(g,p);
 return 0;
}
