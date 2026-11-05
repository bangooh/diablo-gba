/* Devilution CheckInvHLight / GetItemStr keep rarity visible before identify.
 * Identification controls the displayed name, never its quality color.
 * Shops retain S_ScrollSBuy/S_ScrollSSell's requirements-red precedence, with
 * unique gold carried into shops as requested (retail used blue there). */
static UI_FN unsigned item_name_color(const Item *it,unsigned shop){
 if(!it||it->kind>=ITEM_COUNT)return HUD_WHITE;
 if(shop&&(game_effective_stat(0)<item_strength(it)||game_effective_stat(2)<item_defs[it->kind].dexterity||game_effective_stat(1)<item_magic(it)))return HUD_RED;
 if(item_unique(it))return HUD_GOLD;
 return (it->enchant&0x3fff)?HUD_BLUE:HUD_WHITE;
}
