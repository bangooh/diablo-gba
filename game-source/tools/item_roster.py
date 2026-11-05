"""One source-derived binding shared by gameplay, art and sound packers."""
from pathlib import Path
import json, re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source_assets/item_catalog'
ROWS = json.loads((SOURCE/'base_items.json').read_text())['items']
LEGACY_IDS = [0,77,79,81,114,115,135,131,138,139,140,146,48,49,54,55,57,58,59,71,72,109,109]
GEAR_IDS = [r['source_id'] for r in ROWS if r['iRnd'] and r['iLoc'] in range(1,7)]
CONSUMABLE_NAMES = {78:'FULL_HEAL',80:'FULL_MANA',82:'FULL_REJUV',83:'ELIXIR_STRENGTH',84:'ELIXIR_MAGIC',85:'ELIXIR_DEXTERITY',86:'ELIXIR_VITALITY',89:'SCROLL_IDENTIFY'}
IDS = LEGACY_IDS + [i for i in GEAR_IDS if i not in LEGACY_IDS] + list(CONSUMABLE_NAMES) + [6,9,8,16,31,21,28,10,14,15,12,13,7,11,33,17,18,19,20]
LEGACY_NAMES = 'GOLD HEAL MANA REJUV SHORT_SWORD FALCHION CLUB MACE SHORT_BOW HUNTERS_BOW LONG_BOW STAFF CAP SKULL_CAP CAPE RAGS ROBE QUILTED LEATHER BUCKLER SMALL_SHIELD BOOK_FIRE BOOK_HEAL'.split()

def array(name, file):
    source = (SOURCE/file).read_text()
    body = re.search(r'\b'+name+r'\s*\[[^]]*\]\s*=\s*\{(.*?)\};',source,re.S)[1]
    # These original tables only conditionally append/replace Hellfire rows.
    enabled, stack, lines = True, [], []
    for line in body.splitlines():
        directive = line.strip()
        if directive.startswith(('#ifdef ', '#ifndef ')):
            assert directive.split()[1] == 'HELLFIRE'
            stack.append(enabled)
            enabled = enabled and directive.startswith('#ifndef ')
        elif directive == '#else':
            enabled = stack[-1] and not enabled
        elif directive == '#endif':
            enabled = stack.pop()
        elif enabled:
            lines.append(line.split('//')[0])
    assert not stack
    return [t.strip() for t in ','.join(lines).split(',') if t.strip()]

def numbers(name, file):
    result=[]
    for token in array(name,file):
        assert re.fullmatch(r'\d+(?:\s*\*\s*\d+)?',token), token
        factors=token.split('*');result.append(int(factors[0])*(int(factors[1]) if len(factors)>1 else 1))
    return result

WIDTHS=numbers('InvItemWidth','cursor.cpp')
HEIGHTS=numbers('InvItemHeight','cursor.cpp')
ANIMS=numbers('ItemCAnimTbl','items.cpp')
GROUND=[json.loads(v) for v in array('ItemDropNames','items.cpp')]
DROP_SOUNDS=array('ItemDropSnds','items.cpp')
PICKUP_SOUNDS=array('ItemInvSnds','items.cpp')

def binding(kind):
    row=ROWS[IDS[kind]];cursor=row['iCurs']
    # Keep the small ground-gold representation used by the prototype.
    if kind==0:cursor=4
    frame=cursor+12
    return dict(kind=kind,source_id=row['source_id'],cursor=cursor,
                width=WIDTHS[frame]//28,height=HEIGHTS[frame]//28,
                source_width=WIDTHS[frame],source_height=HEIGHTS[frame],
                ground=GROUND[ANIMS[cursor]],
                drop_sound=DROP_SOUNDS[ANIMS[cursor]],pickup_sound=PICKUP_SOUNDS[ANIMS[cursor]])

def symbol(kind):
    if kind<len(LEGACY_NAMES):return 'ITEM_'+LEGACY_NAMES[kind]
    if IDS[kind]==11:return 'ITEM_RING_OF_TRUTH'
    if IDS[kind]==8:return 'ITEM_EMPYREAN_BAND'
    if IDS[kind]==31:return 'ITEM_GRISWOLDS_EDGE'
    if IDS[kind]==28:return 'ITEM_ARKAINES_VALOR'
    if IDS[kind] in CONSUMABLE_NAMES:return 'ITEM_'+CONSUMABLE_NAMES[IDS[kind]]
    row=ROWS[IDS[kind]]
    name=re.sub(r'[^A-Z0-9]+','_',row['iName'].upper()).strip('_')
    if row['itype'] in (12,13):name+='_'+str(row['iMinMLvl'])
    return 'ITEM_'+name

def category(row):
    i=row['source_id']
    if row['iName'] is None:return 'reserved'
    if i in (103,106):return 'unused'
    if i==0:return 'gold'
    if 1<=i<=5:return 'starter'
    if i in GEAR_IDS:return 'ordinary_equipment'
    if row['iRnd']:return 'consumable_or_book'
    if i in (24,25,26,27,29,30,34):return 'vendor_or_fixed_consumable'
    return 'quest_or_unique_reward'

# IPL_INVCURS(44): native cursor dimensions also define bag occupancy.
ICON_UNIQUES = [11, 14, 18, 19, 20, 22, 23, 26, 29, 31, 34, 36, 37, 40, 41, 44, 51, 63, 64, 65, 68, 71, 73, 76, 79, 81, 82, 83, 84, 85, 86, 87, 69, 80, 12, 13, 35, 38, 48, 59, 66, 67, 77, 88, 89, 3, 5, 1, 4]
def art_bindings():
    result=[binding(k) for k in range(len(IDS))]
    uniques=json.loads((SOURCE/'magic_items.json').read_text())['UniqueItemList']
    for uid in ICON_UNIQUES:
        u=uniques[uid]
        kind=next(k for k in range(len(IDS)) if ROWS[IDS[k]]['iItemId']==u['base'])
        b=binding(kind);c=next(u[f'param{i}a'] for i in range(u['count']) if u[f'power{i}']==44)
        sound_kind=next(k for k in range(len(IDS)) if (binding(k)['drop_sound'],binding(k)['pickup_sound'])==(DROP_SOUNDS[ANIMS[c]],PICKUP_SOUNDS[ANIMS[c]]))
        b.update(sound_kind=sound_kind,unique_id=uid,cursor=c,source_width=WIDTHS[c+12],source_height=HEIGHTS[c+12],width=WIDTHS[c+12]//28,height=HEIGHTS[c+12]//28,ground=GROUND[ANIMS[c]])
        result.append(b)
    return result
