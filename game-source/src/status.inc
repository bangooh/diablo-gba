/* Original DrawChr layout, implemented by ui_panels.inc. */
static UI_FN void status_draw(const CharacterState *h,unsigned selected,int readonly);
static UI_FN uint32_t status_revision(const CharacterState *h){
    const uint8_t *data=(const uint8_t*)h;uint32_t hash=2166136261u;
    for(unsigned i=0;i<sizeof(*h);i++)hash=(hash^data[i])*16777619u;
    return hash;
}
