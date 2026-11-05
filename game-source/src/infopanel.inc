/* Source: Devilution DrawInfoBox / PrintMonstHistory / PrintItemDetails.
 * Center every line; wrap at words instead of cutting the right-hand text. */
#include "item_name_color.inc"
static char info_lines[5][23];
/* Reuses the former, unread info_gold word: first N rows are the item name. */
static unsigned info_count,info_name_style;
static UI_FN void info_reset(void){info_count=info_name_style=0;for(unsigned i=0;i<5;i++)info_lines[i][0]=0;}
static UI_FN void info_add(const char *text){
    while(*text&&info_count<5){
        unsigned n=0,last=0;while(text[n]&&n<22){if(text[n]==' ')last=n;n++;}
        if(text[n]&&last)n=last;
        for(unsigned i=0;i<n;i++)info_lines[info_count][i]=text[i];
        info_lines[info_count++][n]=0;text+=n;while(*text==' ')text++;
    }
}
static UI_FN void info_add_name(const Item *item,const char *text){
    info_add(text);info_name_style=info_count|(item_name_color(item,0)<<3);
}
static inline unsigned info_line_color(unsigned row){return row<(info_name_style&7)?info_name_style>>3:HUD_WHITE;}
static UI_FN unsigned info_uint(char *dst,unsigned value){
    char reverse[10];unsigned n=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);
    for(unsigned i=0;i<n;i++)dst[i]=reverse[n-i-1];return n;
}
static UI_FN void info_value(const char *label,unsigned value){
    char line[48];unsigned n=0;while(*label)line[n++]=*label++;
    n+=info_uint(line+n,value);line[n]=0;info_add(line);
}
static UI_FN void info_range(const char *label,unsigned low,unsigned high,char separator){
    char line[48];unsigned n=0;while(*label)line[n++]=*label++;
    n+=info_uint(line+n,low);line[n++]=separator;n+=info_uint(line+n,high);line[n]=0;info_add(line);
}
static UI_FN __attribute__((target("thumb"),minsize)) void info_affix(const Item *it,unsigned suffix){
 ItemPower a=item_power_at(it,suffix);if(a.power<0||a.power==44)return;int power=a.power,value=item_power_value(it,suffix);char line[48];unsigned n=0;
 if(power==16||power==17||power==42||power==43){info_range(power==16||power==42?"FIRE ":"LIGHTNING ",a.low,a.high,'-');return;}
 if(power==65){info_add("NO STRENGTH REQUIRED");return;}
 if(power==68){info_add("ONE HANDED");return;}
 if(power==79){info_value("ARMOR DOWN ",value);return;}
 if(power==52){info_add("HALF TRAP DAMAGE");return;}
 if(power==74){info_add("INFRAVISION");return;}
 if(power==45){info_add("THORNS 1-3 DAMAGE");return;}
 if(power==46){info_add("LOSE ALL MANA");return;}
 if(power==57){info_value("PIERCE ARMOR ",value);return;}
 if(power==62){info_add("RANDOM ARROW SPEED");return;}
 if(power==69){info_add("TRIPLE DEMON MELEE");return;}
 if(power==70){info_add("ALL RESISTANCES ZERO");return;}
 if(power==73){info_add("RANDOM LIFE STEAL");return;}
 if(power==72){info_add("SLOWLY DRAINS LIFE");return;}
 if(power==66){info_add(game_spell_name(a.low));return;}
 if(power==53){info_add("KNOCKBACK");return;}
 if(power==14){info_value(value<0?"SPELL LEVEL DOWN ":"SPELL LEVEL UP ",value<0?-value:value);return;}
 if(power==38||power==39){info_value(value<0?"LIGHT RADIUS DOWN ":"LIGHT RADIUS UP ",value<0?-value:value);return;}
 if(power==37){info_add("INDESTRUCTIBLE");return;}
 if(power==35||power==36){const char *label=power==36?"DUR DOWN ":"DUR UP ";while(*label)line[n++]=*label++;n+=info_uint(line+n,value);line[n++]='%';line[n]=0;info_add(line);return;}
 if(power==15){info_value("CHARGES X",a.low);return;}
 if(power==59){info_value("HIT RECOVERY ",value);return;}
 if(power==60){info_add("FAST BLOCK");return;}
 if(power==58){info_value("ATTACK SPEED ",value);return;}
 if(power==63){info_range("DAMAGE ",a.low,a.high,'-');return;}
 if(power==64){info_value("DURABILITY ",a.low);return;}
 if(power==75){info_value("BASE ARMOR ",value);return;}
 const char *label=power==29||power==30?"TAKE DAMAGE ":power==55?"MANA STEAL ":power==56?"LIFE STEAL ":power<=1?"HIT ":power<=5?"DAMAGE ":power<=7?"ARMOR ":power==8?"FIRE RES ":power==9?"LIGHT RES ":power==10?"MAGIC RES ":power==11?"ALL RES ":power<=20?"STRENGTH ":power<=22?"MAGIC ":power<=24?"DEXTERITY ":power<=26?"VITALITY ":power<=28?"ALL STATS ":power<=32?"LIFE ":power<=34?"MANA ":"DAMAGE ";
 if(power==4||power==5){label="HIT ";while(*label)line[n++]=*label++;int hit=item_affix_secondary(it,suffix);if(hit<0)line[n++]='-';n+=info_uint(line+n,hit<0?-hit:hit);label=" DMG ";while(*label)line[n++]=*label++;if(value<0)line[n++]='-';n+=info_uint(line+n,value<0?-value:value);line[n++]='%';}
 else{while(*label)line[n++]=*label++;label=value<0?"DOWN ":"UP ";while(*label)line[n++]=*label++;n+=info_uint(line+n,value<0?-value:value);if(power<=11||power==55||power==56)line[n++]='%';}
 line[n]=0;info_add(line);
}
static UI_FN void info_render(void){
    if(hud_transparent){
        info_raster_prepare(info_lines,info_count,info_name_style);
        return; /* VBlank publishes glyphs and map as one update. */
    }
    for(unsigned y=15;y<20;y++)for(unsigned x=0;x<30;x++)if(y==15||(x>=4&&x<26))hud_map[y*32+x]=128;
    if(hud_transparent){
        info_stream_stop();return;
    }
    if(!info_count)return;
    unsigned top=20-info_count;
    if(!hud_transparent)for(unsigned y=top;y<20;y++)hud_text(4,y,"                      ");
    for(unsigned i=0;i<info_count;i++){
        unsigned n=0;while(info_lines[i][n])n++;
        int x=4+(22-(int)n)/2;
        hud_text_color(x,top+i,info_lines[i],info_line_color(i));
    }
}
static UI_FN void info_box_oam(void){
    for(unsigned i=14;i<42;i++)oam[i*4]=0x0200;
    if(!info_count)return;
    unsigned top=158-info_count*(hud_transparent?INFO_STEP:8);
    /* A solid32x16 affine2x tile has the same64x32 bounds, freeing24 tiles.
     * Matrix1 lives in unused OAM4..7 fourth words. */
    oam[19]=128;oam[23]=0;oam[27]=0;oam[31]=128;
    for(unsigned row=0;row<2;row++)for(unsigned i=0;i<4;i++){
        unsigned n=34+row*4+i;oam[n*4]=(uint16_t)((top+row*32)|0x4700);oam[n*4+1]=(uint16_t)(i*64|0x8200);oam[n*4+2]=832|(1<<10)|(5<<12);
    }
}
static UI_FN void info_item(const Item *item,int detail){
    if(!item||item->kind>=ITEM_COUNT)return;
    const ItemDef *d=&item_defs[item->kind];
    if(item->kind==ITEM_GOLD){char line[32];unsigned n=info_uint(line,item_gold_amount(item));const char *tail=" GOLD PIECES";while(*tail)line[n++]=*tail++;line[n]=0;info_add(line);return;}
    if(item->kind==ITEM_SCROLL_IDENTIFY){info_add("SCROLL OF IDENTIFY");if(detail){info_add("CHOOSE AN UNKNOWN ITEM");if(detail==1)info_add("R: USE");}return;}
    if(item->enchant&&item_is_staff(item)&&item_spell(item)&&!item_unique(item)){
        char title[64];const ItemAffix *pre=item_affix(item,0);
        if(item_unique(item)){item_name(item,title);info_add_name(item,title);}else if(!item_unidentified(item)&&pre){unsigned n=0;const char *p=pre->name;while(*p&&n<15){char c=*p++;if(c!='\'')title[n++]=c>='a'&&c<='z'?c-32:c;}const char *tail=" STAFF";while(*tail)title[n++]=*tail++;title[n]=0;info_add_name(item,title);}else info_add_name(item,d->name);
        info_add_name(item,game_spell_name(item_spell(item)));if(!detail)return;
        info_range("CHARGES: ",item->charges,item->max_charges,'/');
        if(item_unidentified(item)){info_add("NOT IDENTIFIED");info_add(detail==2?"IDENTIFY TO REVEAL":"USE IDENTIFY SCROLL");return;}
        char line[32];unsigned n=0;const char *p="DMG ";while(*p)line[n++]=*p++;n+=info_uint(line+n,item_weapon_low(item));line[n++]='-';n+=info_uint(line+n,item_weapon_high(item));p=" MAG ";while(*p)line[n++]=*p++;n+=info_uint(line+n,item_magic(item));line[n]=0;info_add(line);info_affix(item,0);return;
    }
    if(item->enchant){
        char title[64];item_name(item,title);info_add_name(item,title);if(!detail)return;
        if(item_unidentified(item)){info_add("NOT IDENTIFIED");info_add(detail==2?"IDENTIFY TO REVEAL":"USE IDENTIFY SCROLL");return;}
        if(item_unique(item)){info_add("UNIQUE ITEM");if(item_max_durability(item)==ITEM_INDESTRUCTIBLE)info_add("INDESTRUCTIBLE");else info_range("DUR: ",item_durability(item),item_max_durability(item),'/');info_add("EFFECTS ON LEFT");return;}
        if(d->durability){char line[40];unsigned n=0;line[n++]=d->slot==EQUIP_HAND?'D':'A';line[n++]=' ';int value=d->slot==EQUIP_HAND?(int)item_weapon_low(item):item_ac(item);if(value<0)line[n++]='-';n+=info_uint(line+n,value<0?-value:value);if(d->slot==EQUIP_HAND){line[n++]='-';n+=info_uint(line+n,item_weapon_high(item));}const char *tail=item_max_durability(item)==ITEM_INDESTRUCTIBLE?" INDESTRUCTIBLE":" DUR ";while(*tail)line[n++]=*tail++;if(item_max_durability(item)!=ITEM_INDESTRUCTIBLE){n+=info_uint(line+n,item_durability(item));line[n++]='/';n+=info_uint(line+n,item_max_durability(item));}line[n]=0;info_add(line);}else{int ac=item_ac(item);info_value(ac<0?"ARMOR: -":"ARMOR: ",ac<0?-ac:ac);}
        for(unsigned k=0;k<2;k++)info_affix(item,k);return;
    }
    if(item_is_spell(item)){char label[64];item_name(item,label);info_add_name(item,label);if(detail){if(!item_scroll(item))info_value("REQUIRES MAGIC: ",game_spell_requirement(item_spell(item)));info_add(detail==2?(item_scroll(item)?"ONE USE SPELL":"LEARN OR IMPROVE SPELL"):"R: USE");}return;}
    if(item->kind>=ITEM_ELIXIR_STRENGTH&&item->kind<=ITEM_ELIXIR_VITALITY){
        info_add_name(item,d->name);if(detail){info_add("PERMANENT +1 STAT");if(detail==1)info_add("R: USE");}return;
    }
    if(game_belt_code(item->kind)){info_add_name(item,d->name);if(detail==1)info_add("R: USE");return;}
    if(item->kind==ITEM_SPECTRAL_ELIXIR){info_add_name(item,d->name);if(detail){info_add("PERMANENT +3 ALL STATS");if(detail==1)info_add("R: USE");}return;}
    if(item_is_staff(item)&&item_spell(item)){
        /* At most two name lines, leaving three for charges/damage/requirements. */
        char title[32];unsigned len=0;const char *name=d->name;while(*name&&len<28)title[len++]=*name++;title[len++]=' ';title[len++]='O';title[len++]='F';title[len]=0;
        info_add_name(item,title);info_add_name(item,game_spell_name(item_spell(item)));if(!detail)return;
        info_range("CHARGES: ",item->charges,item->max_charges,'/');
        char stats[32];unsigned m=0;const char *part="DMG ";while(*part)stats[m++]=*part++;m+=info_uint(stats+m,item_weapon_low(item));stats[m++]='-';m+=info_uint(stats+m,item_weapon_high(item));part=" DUR ";while(*part)stats[m++]=*part++;m+=info_uint(stats+m,item_durability(item));stats[m++]='/';m+=info_uint(stats+m,item_max_durability(item));stats[m]=0;info_add(stats);
        char line[32];unsigned n=0;const char *label="REQ ";while(*label)line[n++]=*label++;
        if(item_strength(item)){n+=info_uint(line+n,item_strength(item));label=" STR ";while(*label)line[n++]=*label++;}
        n+=info_uint(line+n,item_magic(item));label=" MAG";while(*label)line[n++]=*label++;line[n]=0;info_add(line);return;
    }
    info_add_name(item,d->name);if(!detail)return;
    if(d->slot==EQUIP_HAND)info_range("DAMAGE: ",item_weapon_low(item),item_weapon_high(item),'-');
    else if(d->ac_high){int ac=item_ac(item);info_value(ac<0?"ARMOR: -":"ARMOR: ",ac<0?-ac:ac);}
    if(item_max_durability(item)==ITEM_INDESTRUCTIBLE)info_add("INDESTRUCTIBLE");else if(d->durability)info_range("DUR: ",item_durability(item),item_max_durability(item),'/');
    if(item_strength(item)||d->magic||d->dexterity){
        unsigned first=1;const unsigned values[3]={item_strength(item),d->magic,d->dexterity};const char *names[3]={" STR"," MAG"," DEX"};
        for(unsigned i=0;i<3;i++)if(values[i]){char line[32];unsigned n=0;const char *label=first?"REQUIRED: ":"";while(*label)line[n++]=*label++;n+=info_uint(line+n,values[i]);label=names[i];while(*label)line[n++]=*label++;line[n]=0;info_add(line);first=0;}

    }
}
static UI_FN void info_monster(unsigned kind){
    if(kind>=MONSTER_TYPES)return;
    const char *name=kind==2||kind==3?"FALLEN ONE":monster_art[kind].name;
    info_add(name);
    if(kind==MONSTER_BUTCHER||kind==MONSTER_KING||(kind>=MONSTER_LAZARUS&&kind<=MONSTER_DIABLO)){info_add(game_monster_resistances(kind)?"SOME MAGIC RESISTANCES":"NO RESISTANCES");info_add(game_monster_immunities(kind)?"SOME MAGIC IMMUNITIES":"NO IMMUNITIES");return;}
    unsigned kills=monster_history[game_monster_history_index(kind)];info_value("TOTAL KILLS : ",kills);
    if(kills>=30)info_range("HIT POINTS : ",game_monster_hp_bound(kind,0),game_monster_hp_bound(kind,1),'-');
    if(kills>=15){
        unsigned r=game_monster_resistances(kind),im=game_monster_immunities(kind);
        static const char *const resist[8]={"","RESISTS : MAGIC","RESISTS : FIRE","RESISTS : MAGIC/FIRE","RESISTS : LIGHTNING","RESISTS : MAGIC/LIGHT","RESISTS : FIRE/LIGHT","RESISTS : ALL"};
        if(r)info_add(resist[r&7]);
        static const char *const immune[8]={"","IMMUNE : MAGIC","IMMUNE : FIRE","IMMUNE : MAGIC/FIRE","IMMUNE : LIGHTNING","IMMUNE : MAGIC/LIGHT","IMMUNE : FIRE/LIGHT","IMMUNE : ALL"};
        if(im)info_add(immune[im&7]);
        if(!r&&!im)info_add("NO MAGIC RESISTANCE");
    }
}
static UI_FN void info_prop(unsigned prop){
                if(game_prop_is_door(game.props[prop].kind)){
                    info_add(game.props[prop].state?"OPEN DOOR":"CLOSED DOOR");
                    info_add(game.props[prop].state?"A: CLOSE":"A: OPEN");return;
                }
                if(game.props[prop].kind>=PROP_SHRINE_L){const char *name=game_environment_name(&game,(unsigned)prop);if(name){info_add(name);return;}}
                if(game.props[prop].kind==PROP_WATER_ENTRY){info_add("TO A DARK PASSAGE");info_add("A: ENTER");return;}
                if(game.props[prop].kind==PROP_KING_ENTRY){info_add("TO THE KINGS LAIR");info_add("A: ENTER");return;}
                if(game.props[prop].kind==PROP_BONE_ENTRY){info_add("CHAMBER OF BONE");if(quest_progress.stage[14])info_add("A: ENTER");return;}
                if(game.props[prop].kind==PROP_VILE_ENTRY){
                    unsigned ready=dungeon_level==15?quest_progress.stage[15]>=2:quest_progress.state[15]==3;
                    info_add(ready?(dungeon_level==15?"TO LAZARUS":"RETURN TO LEVEL 15"):"PORTAL IS INACTIVE");
                    if(ready)info_add("A: ENTER");return;
                }
                if(game.props[prop].kind==PROP_KING_LEVER){info_add("LEVER");return;}
                if(game.props[prop].kind>=PROP_KING_CRUX1&&game.props[prop].kind<=PROP_KING_CRUX3){info_add("CRUCIFIED SKELETON");return;}
                const Prop *p=&game.props[prop];const char *name=p->kind==PROP_BONE_BOOK?"BOOK OF BONE":p->kind==PROP_WARLORD_BOOK?"STEEL TOME":p->kind==PROP_BONE_TOME?"ANCIENT TOME":p->kind==PROP_BONE_LEVER?"SKULL LEVER":p->kind==PROP_WAR_ARMOR?"ARMOR STAND":p->kind==PROP_WAR_WEAPON?"WEAPON RACK":p->kind==PROP_MUSHROOM_PATCH?"MUSHROOM PATCH":p->kind==PROP_VILE_BOOK?"BOOK OF VILENESS":p->kind==PROP_LAZ_STAND?"STAFF OF LAZARUS":p->kind==PROP_DIABLO_LEVER?"SKULL LEVER":p->kind==PROP_SNOTSPIL?"SNOTSPILL":p->kind==PROP_SIGN_CHEST?"TAVERN SIGN CHEST":p->kind==PROP_BLIND_BOOK?"BOOK OF THE BLIND":p->kind==PROP_LACHDANAN?"LACHDANAN":p->kind==PROP_VALOR_BOOK?"BOOK OF BLOOD":p->kind==PROP_VALOR_PEDESTAL?"BLOOD STONE PEDESTAL":p->kind==PROP_SARC?"SARCOPHAGUS":p->kind==PROP_BARREL?"BARREL":p->kind==PROP_CHEST?"SMALL CHEST":p->kind==PROP_CHEST_MEDIUM?"CHEST":p->kind==PROP_CHEST_LARGE?"LARGE CHEST":p->state?"OPEN DOOR":"CLOSED DOOR";info_add(name);if(game_hero_class()==1&&game_prop_trapped(&game,(unsigned)prop))info_add("TRAPPED");

}
static UI_FN __attribute__((target("thumb"),minsize)) void info_player(unsigned player){
    const CharacterState *h=&coop_players[player].hero;info_value("PLAYER ",player+1);info_value("LEVEL : ",h->progress.level);
    char line[40];unsigned n=0;const char *label="HIT POINTS ";while(*label)line[n++]=*label++;n+=info_uint(line+n,h->hp>0?(unsigned)h->hp:0);label=" OF ";while(*label)line[n++]=*label++;n+=info_uint(line+n,game_character_max_hp(h));line[n]=0;info_add(line);
}
static UI_FN __attribute__((target("thumb"),minsize)) void info_world(void){
    info_reset();
    /* Only an accepted Heal Other / Resurrect cast raises ally priority.
     * Reading the actual spell recipient also excludes dead healing targets
     * and living resurrection targets, unlike the generic proximity panel. */
    int player=game_spell_player_target(&game);
    if(player>=0){info_player((unsigned)player);info_render();return;}
    /* Current targeting wins over the lingering ground-drop notice. */
    int target=game.attack_wait&&!SPELL_CASTING(game.swing_target)?game.swing_target:game.target;
    if(!game.area&&target>=0&&target<MAX_ENEMIES&&game.enemies[target].alive){info_monster(game.enemies[target].kind);info_render();return;}
    if(!game.area&&target<=-2&&target>=-65&&game.props[-target-2].kind){info_prop((unsigned)(-target-2));info_render();return;}
    if(target==TARGET_ITEM){int item=game_item_target(&game);if(item>=0){Item it=item_from_drop(&game.drops[item]);info_item(&it,0);info_render();return;}}
    const Drop *recent=target==TARGET_PORTAL?0:game_drop_notice(&game);
    if(recent){Item it=item_from_drop(recent);info_item(&it,0);info_render();return;}
    if(!game.area&&game.pickup_notice_ticks){
        unsigned spell=(unsigned)game.pickup_notice-GAME_BOOK_NOTICE_BASE;
        if(game.pickup_notice>=GAME_BOOK_NOTICE_BASE&&game_spell_supported(spell)){
            Item book={0};book.kind=ITEM_BOOK_FIRE;book.flags=(uint8_t)spell;
            info_item(&book,0);info_render();return;
        }
        const char *notice=game_environment_notice(game.pickup_notice);
        if(notice){info_add(notice);info_render();return;}
    }
    int portal_owner=game_spell_portal_target(&game);
    if(portal_owner>=0){
        info_add("TOWN PORTAL");
        if(game.area){unsigned level=(unsigned)game_spell_portal_destination(portal_owner);
            const char *name=level==REGION_WATER?"TO WATER SUPPLY":level==REGION_KING?"TO KING LAIR":level==REGION_BONE?"TO BONE LAIR":level==REGION_VILE?"TO VILE LAIR":0;
            if(name)info_add(name);else info_value("TO LEVEL ",level);
        }else info_add("TO TOWN");
        info_add("A: ENTER");info_render();return;
    }
    int shortcut=game_shortcut_target(&game);
    if(shortcut){info_add(game.area?(shortcut==5?"DOWN TO CATACOMBS":shortcut==9?"DOWN TO CAVES":"DOWN TO HELL"):"UP TO TOWN");info_add("A: ENTER");info_render();return;}

    /* Trigger text takes precedence in retail DrawInfoBox. */
    if(game.area){
        if(highlighted_drop>=0&&highlighted_drop<MAX_DROPS&&game.drops[highlighted_drop].active){Item it=item_from_drop(&game.drops[highlighted_drop]);info_item(&it,0);}
        else if(game.x>=23&&game.x<=27&&game.y>=27&&game.y<=31){info_add("DOWN TO DUNGEON");info_add("A: ENTER");}
        else{int npc=nearby_npc();if(npc>=0)info_add(npc_names[npc]);}
    }else{
        int exit=0;
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int x=game.x+dx,y=game.y+dy;if(x>=0&&y>=0&&x<80&&y<80&&game.map[y][x]==2)exit=1;}
        if(exit){if(dungeon_level==1)info_add("UP TO TOWN");else info_value("UP TO LEVEL ",dungeon_level==REGION_BONE?6:dungeon_level==REGION_KING?3:dungeon_level==REGION_WATER?2:dungeon_level-1);info_add("A: ENTER");}
        else if(game.down_x&&game.x>=game.down_x-2&&game.x<=game.down_x+2&&game.y>=game.down_y-2&&game.y<=game.down_y+2){if(dungeon_level==15&&quest_progress.state[15]!=3)info_add("DEFEAT LAZARUS FIRST");else if(dungeon_level<16){info_value("DOWN TO LEVEL ",dungeon_level+1);info_add("A: ENTER");}else info_add("LOWER FLOORS NOT READY");}
        else{
            int target=game.attack_wait?game.swing_target:game.target;
            int prop=game_prop_target(&game),item=highlighted_drop;
            if(target>=0&&target<MAX_ENEMIES&&game.enemies[target].alive)info_monster(game.enemies[target].kind);
            else if(prop>=0)info_prop((unsigned)prop);
            else if(item>=0&&item<MAX_DROPS&&game.drops[item].active){const Drop *d=&game.drops[item];Item it=item_from_drop(d);info_item(&it,0);}
        }
    }
    if(!info_count&&!game.area){
        for(unsigned i=0;i<MAX_PROPS;i++){const Prop *p=&game.props[i];int dx=p->x-game.x,dy=p->y-game.y;
            if(game_prop_is_door(p->kind)&&dx>=-1&&dx<=1&&dy>=-1&&dy<=1&&game_prop_direction(&game,(int)i)>=0){info_prop(i);break;}}
    }
    if(!info_count&&coop_active){
        /* Recovery/empty swings can retain no target while a real monster or
         * pickup is nearby. Preserve their read-only information fallback.
         * Ally information is shown only for an accepted support cast above;
         * ordinary proximity never selects an ally or keeps its HUD active. */
        int enemy=game_ranged_target(&game,0),item=enemy<0?game_item_target(&game):-1;
        if(enemy>=0)info_monster(game.enemies[enemy].kind);
        else if(item>=0){Item it=item_from_drop(&game.drops[item]);info_item(&it,0);}
    }
    info_render();
}
