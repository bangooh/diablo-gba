"""Compact retail difficulty metadata, without rewriting artwork/catalogs.

Usage: python3 tools/pack_difficulty.py [--check]

The hand-written conditional parser is checked against a C compiler reading
the original conditional tables. The native InitMonster/PlaceUniqueMonst
difficulty statements are also compiled verbatim as an independent oracle.
Only generated/monster_difficulty* files are written.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = '9f01757243412bb154d02d9af65c804159e6e0ea'
PINNED = {
    'source_assets/monster_catalog/monstdat.cpp': '3c57132ce04b628a892806eea14aa85189d2b55efd5c207fc0d9aebce8428532',
    'source_assets/item_catalog/enums.h': '35b581333fc8a1adaed4ed7c0519c8802fa2062592ccd1ad162701c58a856953',
    'source_assets/item_catalog/monster.cpp': 'afc7b7b00c2e17e9d26e0344c9ac39cae9343e77e876e5ff616973e94870006b',
}
WIDTH_SOURCE = 'source_assets/town_dialogue_stage53/structs.h'
WIDTH_SOURCE_SHA = '2fa73116f5cc17eac219bd3dd46affbff37870271a8a9e2fc3a3fe98516e99b7'
WIDTH_PROVENANCE = {
    'url': 'https://github.com/diasurgical/devilution/blob/'+REVISION+'/structs.h',
    'pinned_revision_verified': True,
    'limitation': 'Selected scalar declarations; full MonsterStruct layout is not modeled.',
    'native_compiler': 'Win32 MSVC; plain char modeled as signed char',
    'fields': {'mLevel': 'signed8 char', 'mExp': 'unsigned16 short',
               'mHit': 'unsigned8 char', 'mHit2': 'unsigned8 char',
               'mMinDamage': 'unsigned8 char', 'mMaxDamage': 'unsigned8 char',
               'mMinDamage2': 'unsigned8 char', 'mMaxDamage2': 'unsigned8 char',
               'mArmorClass': 'unsigned8 char (retail #else)'},
}
QUESTS = {6: 'The Butcher', 29: 'Snotspill', 33: 'Skeleton King',
          41: 'Arch-Bishop Lazarus', 42: 'Red Vex', 43: 'BlackJade',
          46: 'Gharbad the Weak', 47: 'Zhar the Mad', 48: 'Warlord of Blood'}
BASE_FIELDS = ('hp_min', 'hp_max', 'xp', 'level', 'hit', 'armor',
               'damage_min', 'damage_max', 'hit2', 'damage2_min', 'damage2_max',
               'resists', 'hell_resists')
UNIQUE_FIELDS = ('hp', 'level', 'damage_min', 'damage_max', 'resists', 'flags', 'var1')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def retail(text):
    """Strictly process the conditional forms used by the pinned tables."""
    stack, enabled, output = [], True, []
    for line in text.splitlines():
        match = re.match(r'\s*#(ifdef|ifndef)\s+(\w+)\s*$', line)
        if match:
            op, name = match.groups()
            assert name in ('HELLFIRE', 'SPAWN'), name
            condition = op == 'ifndef'  # Neither build flag is defined.
            stack.append((enabled, condition, False))
            enabled = enabled and condition
        elif re.match(r'\s*#else\s*$', line):
            parent, condition, seen_else = stack[-1]
            assert not seen_else
            stack[-1] = (parent, condition, True)
            enabled = parent and not condition
        elif re.match(r'\s*#endif\s*$', line):
            enabled, _, _ = stack.pop()
        elif re.match(r'\s*#', line):
            assert re.match(r'\s*#(?:include|define)\b', line), line
        elif enabled:
            output.append(line)
    assert not stack
    return '\n'.join(output)


def fields(text):
    result, start, depth, quoted, escaped = [], 0, 0, False, False
    for i, c in enumerate(text):
        if quoted:
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif c == ',' and not depth:
            result.append(text[start:i].strip())
            start = i + 1
    result.append(text[start:].strip())
    return result


def table(source, declaration):
    body = source.split(declaration, 1)[1].split('\n};', 1)[0]
    return [fields(line[line.index('{') + 1:line.rindex('}')])
            for line in body.splitlines() if re.match(r'\s*\{\s*(?:\d+|MT_)', line)]


def original_table(source, declaration):
    return declaration + source.split(declaration, 1)[1].split('\n};', 1)[0] + '\n};\n'


def traits(expression):
    names = ('MAGIC', 'FIRE', 'LIGHTNING')
    immune = sum(1 << i for i, n in enumerate(names) if re.search(r'\bIMMUNE_' + n + r'\b', expression))
    resist = sum(1 << i for i, n in enumerate(names) if re.search(r'\bRESIST_' + n + r'\b', expression))
    return immune | (resist << 3)


def base_record(row):
    assert len(row) == 34
    positions = {'hp_min': 14, 'hp_max': 15, 'xp': 33, 'level': 13, 'hit': 19,
                 'armor': 27, 'damage_min': 21, 'damage_max': 22,
                 'hit2': 23, 'damage2_min': 25, 'damage2_max': 26}
    result = {name: int(row[index], 0) for name, index in positions.items()}
    result.update(resists=traits(row[29]), hell_resists=traits(row[30]))
    return result


def unique_record(row):
    assert len(row) == 14
    return dict(hp=int(row[4]), level=int(row[3]), damage_min=int(row[7]),
                damage_max=int(row[8]), resists=traits(row[9]), flags=int(row[10]), var1=int(row[11]))


def enum_ids(source):
    body = retail(source).split('typedef enum _monster_id {', 1)[1].split('}', 1)[0]
    return {key: int(value, 0) for key, value in re.findall(r'\b(MT_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', body)}


def signature(row):
    return tuple(row[i] for i in (2, 7, 10, 16))


def build_records(source, enums, catalog, sidecar):
    rows = table(retail(source), 'MonsterData monsterdata[] = {')
    all_rows = table(source, 'MonsterData monsterdata[] = {')
    unique_rows = table(retail(source), 'UniqMonstStruct UniqMonst[] = {')
    ids = enum_ids(enums)
    assert len(rows) == 112 and ids['MT_DIABLO'] == 110
    assert base_record(rows[110])['hp_min'] == 1666
    lookup = {json.loads(row[1]): (i, row) for i, row in enumerate(unique_rows)}
    all_catalog = catalog['monsters'] + sidecar['monsters']
    assert len(all_catalog) == 242 and [d['kind'] for d in all_catalog] == list(range(242))
    bases, uniques, mapping, provenance = [], [], [], []
    base_indices, unique_indices, native_by_kind = {}, {}, {}
    for d in all_catalog:
        kind, unique_name = d['kind'], QUESTS.get(d['kind'])
        if d.get('role') == 'leader':
            unique_name = d['name']
        native_unique, unique_index = -1, 0
        if unique_name:
            native_unique, unique_row = lookup[unique_name]
            native = ids[unique_row[0]]
            if native_unique not in unique_indices:
                unique_indices[native_unique] = len(uniques) + 1
                uniques.append(unique_record(unique_row))
            unique_index = unique_indices[native_unique]
        elif d.get('role') == 'minion':
            native = next(i for i,row in enumerate(rows) if signature(row)==signature(all_rows[all_catalog[d['base_kind']]['source_id']]))
            unique_index = 128
        else:
            # Old source_id counted both preprocessor branches. Match the native
            # source identity after removing expansion-only branches instead.
            old = all_rows[d['source_id']]
            matches = [i for i, row in enumerate(rows) if signature(row) == signature(old)]
            assert len(matches) == 1, (d['key'], matches)
            native = matches[0]
        native_by_kind[kind] = native
        if native not in base_indices:
            base_indices[native] = len(bases)
            bases.append(base_record(rows[native]))
        mapping.append((base_indices[native], unique_index))
        provenance.append(dict(kind=kind, key=d['key'], native_id=native,
                               native_name=json.loads(rows[native][10]),
                               native_unique_index=native_unique,
                               unique_name=unique_name, base=base_indices[native], unique=unique_index))
    assert len(uniques) == 81
    return rows, unique_rows, bases, uniques, mapping, provenance


def c_oracle(source, enums, code, rows, unique_rows, bases, uniques, mapping, provenance, all_rolls=False):
    """Compile untouched native conditional tables and difficulty statements.

    Return cases[(kind,difficulty,players,sample)] with values
    [hp_q6,level,xp,hit,armor,damage_min,damage_max,damage2_min,damage2_max,
     packed_resists,hit2]. Normally sample0/1 means minimum/maximum HP.
    With all_rolls=True, sample is the native base HP roll index (0..range-1).
    Runtime tests can call this to test real getters against original C code.
    """
    monster_enum = 'typedef enum _monster_id {' + enums.split('typedef enum _monster_id {', 1)[1].split('} _monster_id;', 1)[0] + '} _monster_id;\n'
    tables = original_table(source, 'MonsterData monsterdata[] = {') + original_table(source, 'UniqMonstStruct UniqMonst[] = {')
    tokens = set(re.findall(r'\b(?:AI_|MFLAG_|MC_|TEXT_)\w+', tables))
    defines = '\n'.join('#define ' + token + ' 0' for token in sorted(tokens))
    init = code.split('void InitMonster(', 1)[1].split('\nvoid ClrAllMonsters', 1)[0]
    init_diff = init[init.index('\tif (gnDifficulty == DIFF_NIGHTMARE)'):].rsplit('\n}', 1)[0]
    unique_code = code.split('void PlaceUniqueMonst(', 1)[1].split('\nvoid ', 1)[0]
    unique_setup = unique_code[unique_code.index('\tif (Uniq->mlevel)'):unique_code.index('\n#ifdef HELLFIRE', unique_code.index('\tMonst->mtalkmsg ='))]
    unique_diff = unique_code[unique_code.index('\tif (gnDifficulty == DIFF_NIGHTMARE)'):unique_code.index('\n\tsprintf(filestr')]
    unique_attr = unique_code[unique_code.index('\tif (Uniq->mUnqAttr & 4)'):unique_code.index('\n\tnummonsters++;')]
    width_bytes = (ROOT / WIDTH_SOURCE).read_bytes()
    assert WIDTH_SOURCE_SHA and sha(width_bytes) == WIDTH_SOURCE_SHA
    native_struct = width_bytes.decode().split('typedef struct MonsterStruct {', 1)[1].split('} MonsterStruct;', 1)[0]
    declarations_by_name = dict((name, kind) for kind, name in re.findall(r'^\s*((?:unsigned )?(?:char|short))\s+(\w+)\s*;', retail(native_struct), re.M))
    names = list(WIDTH_PROVENANCE['fields'])
    assert all(name in declarations_by_name for name in names)
    width_body = '\n'.join(declarations_by_name[name]+' '+name+';' for name in names)
    declarations = r'''
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
_Static_assert(CHAR_BIT==8 && CHAR_MIN==-128 && USHRT_MAX==65535 && INT_MAX==2147483647,
               "native Win32 scalar widths");
#define TRUE 1
#define FALSE 0
#define DIFF_NIGHTMARE 1
#define DIFF_HELL 2
#define RESIST_MAGIC 1
#define RESIST_FIRE 2
#define RESIST_LIGHTNING 4
#define IMMUNE_MAGIC 8
#define IMMUNE_FIRE 16
#define IMMUNE_LIGHTNING 32
#define IMMUNE_NULL_40 64
#define IMMUNE_ACID 128
typedef struct {int width,image; const char *graphic; int special; const char *sound;
 int snd_special,has_trans; const char *translation; int frames[6],rates[6]; const char *name;
 int min_floor,max_floor,mLevel,hp_min,hp_max,ai,flags,intelligence,mHit,impact,
 mMinDamage,mMaxDamage,mHit2,impact2,mMinDamage2,mMaxDamage2,mArmorClass,cls,mMagicRes,mMagicRes2,treasure,sel,mExp;
} MonsterData;
typedef struct {int mtype;const char *mName,*translation;int mlevel,mmaxhp,mAi,mint,
 mMinDamage,mMaxDamage,mMagicRes,mUnqAttr,mUnqVar1,var2,mtalkmsg;} UniqMonstStruct;
typedef struct {int _mmaxhp,_mhitpoints;
''' + width_body + r'''
 int mMagicRes,_mAi,_mint,mtalkmsg;const char *mName;} Monster;
_Static_assert(sizeof(((Monster*)0)->mLevel)==1 && sizeof(((Monster*)0)->mExp)==2 &&
               sizeof(((Monster*)0)->mHit)==1 && sizeof(((Monster*)0)->mHit2)==1 &&
               sizeof(((Monster*)0)->mArmorClass)==1 && sizeof(((Monster*)0)->mMinDamage)==1 &&
               sizeof(((Monster*)0)->mMaxDamage)==1 && sizeof(((Monster*)0)->mMinDamage2)==1 &&
               sizeof(((Monster*)0)->mMaxDamage2)==1,"native MonsterStruct stat widths");
static int packed(int native){return ((native>>3)&7)|((native&7)<<3);}
'''
    ids = ','.join('{' + str(p['native_id']) + ',' + str(p['native_unique_index']) + ',' + str(int(p['unique'] == 128)) + '}' for p in provenance)
    harness = declarations + defines + '\n' + monster_enum + tables + r'''
int main(void){
 for(unsigned n=0;n<sizeof(monsterdata)/sizeof(*monsterdata);n++){
  MonsterData *b=&monsterdata[n];
  printf("B %u %d %d %d %d %d %d %d %d %d %d %d %d %d\n",n,b->hp_min,b->hp_max,b->mExp,b->mLevel,b->mHit,b->mArmorClass,b->mMinDamage,b->mMaxDamage,b->mHit2,b->mMinDamage2,b->mMaxDamage2,packed(b->mMagicRes),packed(b->mMagicRes2));
 }
 for(unsigned n=0;n<sizeof(UniqMonst)/sizeof(*UniqMonst);n++){
  UniqMonstStruct *u=&UniqMonst[n];if(u->mtype<0)continue;
  printf("U %u %d %d %d %d %d %d %d\n",n,u->mmaxhp,u->mlevel,u->mMinDamage,u->mMaxDamage,packed(u->mMagicRes),u->mUnqAttr,u->mUnqVar1);
 }
''' + 'const int kinds[][3]={' + ids + '};\nconst int all_rolls=' + str(int(all_rolls)) + ';\n' + r'''
 for(unsigned kind=0;kind<sizeof(kinds)/sizeof(kinds[0]);kind++)for(int gnDifficulty=0;gnDifficulty<3;gnDifficulty++)for(int gbMaxPlayers=1;gbMaxPlayers<=2;gbMaxPlayers++)for(int endpoint=0;endpoint<(all_rolls?monsterdata[kinds[kind][0]].hp_max-monsterdata[kinds[kind][0]].hp_min+1:2);endpoint++){
  MonsterData *b=&monsterdata[kinds[kind][0]];
  struct {MonsterData *MData;} holder={b},*monst=&holder;
  int raw=all_rolls?b->hp_min+endpoint:endpoint?b->hp_max:b->hp_min;
  Monster monster[1]={{0}},*Monst=&monster[0];int i=0;
  Monst->_mmaxhp=raw<<6;if(gbMaxPlayers==1){Monst->_mmaxhp>>=1;if(Monst->_mmaxhp<64)Monst->_mmaxhp=64;}
  Monst->_mhitpoints=Monst->_mmaxhp;Monst->mLevel=b->mLevel;Monst->mExp=b->mExp;
  Monst->mHit=b->mHit;Monst->mHit2=b->mHit2;Monst->mArmorClass=b->mArmorClass;
  Monst->mMinDamage=b->mMinDamage;Monst->mMaxDamage=b->mMaxDamage;Monst->mMinDamage2=b->mMinDamage2;Monst->mMaxDamage2=b->mMaxDamage2;Monst->mMagicRes=b->mMagicRes;
''' + init_diff + '\nif(kinds[kind][1]>=0){UniqMonstStruct *Uniq=&UniqMonst[kinds[kind][1]];\n' + unique_setup + unique_diff + unique_attr + r'''
 }
 if(kinds[kind][2])Monst->_mmaxhp*=2;
 printf("C %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",kind,gnDifficulty,gbMaxPlayers,endpoint,
  Monst->_mmaxhp,Monst->mLevel,Monst->mExp,Monst->mHit,Monst->mArmorClass,Monst->mMinDamage,Monst->mMaxDamage,Monst->mMinDamage2,Monst->mMaxDamage2,packed(Monst->mMagicRes),Monst->mHit2);
 }
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='difficulty-source-oracle-') as directory:
        path = Path(directory)
        (path / 'oracle.c').write_text(harness)
        subprocess.run(['gcc', '-std=c11', '-O0', '-fsigned-char', '-Wall', '-Werror', '-Wno-missing-braces',
                        '-Wno-unused-variable', '-UHELLFIRE', '-USPAWN', str(path / 'oracle.c'),
                        '-o', str(path / 'oracle')], check=True, text=True)
        output = subprocess.check_output([str(path / 'oracle')], text=True)
    cases = {}
    for line in output.splitlines():
        tag, *values = line.split()
        numbers = list(map(int, values))
        if tag == 'B':
            assert numbers[1:] == [base_record(rows[numbers[0]])[name] for name in BASE_FIELDS]
        elif tag == 'U':
            assert numbers[1:] == [unique_record(unique_rows[numbers[0]])[name] for name in UNIQUE_FIELDS]
        else:
            assert tag == 'C'
            cases[tuple(numbers[:4])] = numbers[4:]
    expected_cases = 6 * sum((bases[bi]['hp_max']-bases[bi]['hp_min']+1) if all_rolls else 2 for bi,ui in mapping)
    assert len(cases) == expected_cases
    # This independent calculation checks that the compact representation can
    # reproduce the verbatim native oracle, including unique override order.
    for (kind, difficulty, players, high), actual in cases.items():
        bi, ui = mapping[kind]
        b, u = bases[bi], uniques[ui-1] if 0 < ui < 128 else None
        raw = b['hp_min']+high if all_rolls else b['hp_max' if high else 'hp_min']
        hp = (u['hp'] if u else raw) * (32 if players == 1 else 64)
        hp = max(64, hp)
        if difficulty:
            hp = hp * (3 if difficulty == 1 else 4) + (64 if difficulty == 1 else 192)
        if ui == 128:
            hp *= 2
        shift = difficulty * 15
        signed8 = lambda value: ((value+128)&255)-128
        level = signed8(b['level']+shift)
        if u:
            level = signed8(2*u['level'] if u['level'] else level+5)
            level = signed8(level+shift)
        xp = b['xp']
        if difficulty:
            xp = ((xp+1000)*(2 if difficulty == 1 else 4))&65535
        if u:
            xp = (xp*2)&65535
            if difficulty:
                xp = ((xp+1000)*(2 if difficulty == 1 else 4))&65535
        hit = (u['var1'] if u and u['flags'] & 4 else b['hit'] + (0,85,120)[difficulty])&255
        hit2 = (u['var1'] if u and u['flags'] & 4 else b['hit2'] + (0,85,120)[difficulty])&255
        armor = (u['var1'] if u and u['flags'] & 8 else b['armor'] + (0,50,80)[difficulty])&255
        damages = [u['damage_min'],u['damage_max']]*2 if u else [b[n] for n in ('damage_min','damage_max','damage2_min','damage2_max')]
        damages = [(d if not difficulty else 2*(d+2) if difficulty==1 else 4*d+6)&255 for d in damages]
        resist = u['resists'] if u else b['hell_resists' if difficulty==2 else 'resists']
        assert actual == [hp,level,xp,hit,armor,*damages,resist,hit2], (kind,difficulty,players,high,actual)
    return cases, sha(harness.encode())


def manual_boundaries(bases, uniques, mapping, cases):
    """Fixed independent source readings; never derive these from catalog data."""
    # kind: normal unique level, normal XP, raw uniqueHP, normal hit, normalAC.
    expected = {6:(6,1420,220,50,50),29:(8,510,220,30,15),33:(14,1140,240,60,70),
                41:(35,9936,600,120,0),42:(33,8960,400,115,75),43:(33,8960,400,115,75),
                46:(8,920,120,50,40),47:(16,8140,360,90,0),48:(26,8504,850,120,80),
                50:(4,136,91,100,0),51:(6,116,102,10,45),52:(18,2808,351,70,45),53:(20,3618,525,90,90)}
    for kind, (level,xp,hp,hit,armor) in expected.items():
        result=cases[kind,0,2,0]
        assert result[:5] == [hp*64,level,xp,hit,armor], (kind,result)
    # Independent manual source audit: MP Hell hp,level,XP,hit,AC.
    hell = {6:(883,66,58720,170,130),29:(883,38,44160,150,95),
            33:(963,74,54240,180,150),41:(2403,95,63904,240,80),
            42:(1603,93,48288,235,155),43:(1603,93,48288,235,155),
            46:(483,38,50720,170,120),47:(1443,46,35168,210,80),
            48:(3403,56,40992,240,160),50:(367,34,38176,100,80),
            51:(411,36,37856,130,45),52:(1407,48,15392,190,125),
            53:(2103,50,28352,210,90)}
    for kind,(hp,level,xp,hit,armor) in hell.items():
        assert cases[kind,2,2,0][:5] == [hp*64,level,xp,hit,armor]
    # Expansion contamination, SP half-point rounding, pack order, and unique
    # double-XP/fixed-attribute behavior are explicit historical boundaries.
    assert cases[44,0,2,0][:5] == [1666*64,30,31666,220,90]
    assert cases[44,1,2,0][:5] == [4999*64,45,65332,49,140]
    assert cases[44,2,2,0][:5] == [6667*64,60,65128,84,170]
    assert cases[0,1,1,1][0] == 736  # 7/2*3+1 =11.5; ceil12, not13.
    assert cases[54,1,1,1][0] == 1472  # Member doubles after difficulty:23.
    assert cases[50,2,2,0][2:5] == [38176,100,80]
    assert cases[50,2,2,0][10] == 100
    assert cases[51,2,2,0][4] == 45 and cases[53,2,2,0][4] == 90
    assert cases[41,2,2,0][1:3] == [95,63904]
    assert cases[46,2,2,0][:5] == [483*64,38,50720,170,120]
    # Direct enum-to-body readings: Gharbad uses Flesh Clan and Warlord Steel Lord.
    assert bases[mapping[46][0]]['xp'] == 460 and bases[mapping[48][0]]['xp'] == 4252
    assert uniques[mapping[50][1]-1]['flags'] == 7
    return expected


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    texts={}
    for name, digest in PINNED.items():
        data=(ROOT/name).read_bytes()
        assert sha(data)==digest, 'pinned source changed: '+name
        texts[name]=data.decode()
    catalog_path=ROOT/'source_assets/monster_catalog/monsters.json'
    sidecar_path=ROOT/'source_assets/monster_catalog/uniques.json'
    catalog=json.loads(catalog_path.read_text());sidecar=json.loads(sidecar_path.read_text())
    sidecar['monsters']=json.loads((ROOT/'source_assets/monster_stage71/catalog.json').read_text())['records'][50:]
    assert catalog['source_revision']==sidecar['source_revision']==REVISION
    source=texts['source_assets/monster_catalog/monstdat.cpp'];enums=texts['source_assets/item_catalog/enums.h']
    rows, native_uniques, bases, uniques, mapping, provenance=build_records(source,enums,catalog,sidecar)
    cases, oracle_hash=c_oracle(source,enums,texts['source_assets/item_catalog/monster.cpp'],rows,native_uniques,bases,uniques,mapping,provenance)
    boundaries=manual_boundaries(bases,uniques,mapping,cases)
    for b in bases:
        assert all(0<=b[n]<=65535 for n in BASE_FIELDS[:3])
        assert all(0<=b[n]<=255 for n in BASE_FIELDS[3:])
    for u in uniques:
        assert 0<u['hp']<=65535 and all(0<=u[n]<=255 for n in UNIQUE_FIELDS[1:])
    header='''/* Generated by tools/pack_difficulty.py; retail, HELLFIRE/SPAWN undefined.
 * HP is native raw HP before SP halving/difficulty/pack scaling. XP and level
 * belong to the original base species, before unique overrides.
 * Packed resists: immune bits0..2, resistance bits3..5 (magic/fire/lightning).
 * unique:0 ordinary,1..N unique index+1,0x80 authored pack member.
 * Unique.level is native mlevel: zero inherits base+5, otherwise doubles.
 * Unique.flags preserves mUnqAttr:4 fixes hit,8 fixes armor, after difficulty.
 */
#pragma once
#include <stdint.h>
'''+f'#define MONSTER_DIFFICULTY_TYPES {len(mapping)}\n#define MONSTER_DIFFICULTY_BASES {len(bases)}\n#define MONSTER_DIFFICULTY_UNIQUES {len(uniques)}\n'+'''#define MONSTER_DIFFICULTY_PACK 0x80u
typedef struct {
 uint16_t hp_min,hp_max,xp;
 uint8_t level,hit,armor,damage_min,damage_max,hit2,damage2_min,damage2_max,resists,hell_resists;
} MonsterDifficultyBase;
typedef struct {
 uint16_t hp;
 uint8_t level,damage_min,damage_max,resists,flags,var1;
} MonsterDifficultyUnique;
typedef struct {uint8_t base,unique;} MonsterDifficultyKind;
extern const MonsterDifficultyBase monster_difficulty_bases[MONSTER_DIFFICULTY_BASES];
extern const MonsterDifficultyUnique monster_difficulty_uniques[MONSTER_DIFFICULTY_UNIQUES];
extern const MonsterDifficultyKind monster_difficulty_kinds[MONSTER_DIFFICULTY_TYPES];
_Static_assert(sizeof(MonsterDifficultyBase)==16,"difficulty base layout");
_Static_assert(sizeof(MonsterDifficultyUnique)==8,"difficulty unique layout");
_Static_assert(sizeof(MonsterDifficultyKind)==2,"difficulty kind layout");
'''
    inc=['/* Generated by tools/pack_difficulty.py. Include once in the runtime. */','#include "monster_difficulty.h"',
         'const MonsterDifficultyBase monster_difficulty_bases[MONSTER_DIFFICULTY_BASES]={']
    inc += ['{'+','.join(str(b[n]) for n in BASE_FIELDS)+'},' for b in bases]
    inc += ['};','const MonsterDifficultyUnique monster_difficulty_uniques[MONSTER_DIFFICULTY_UNIQUES]={']
    inc += ['{'+','.join(str(u[n]) for n in UNIQUE_FIELDS)+'},' for u in uniques]
    inc += ['};','const MonsterDifficultyKind monster_difficulty_kinds[MONSTER_DIFFICULTY_TYPES]={']
    inc += ['{'+str(b)+','+str(u)+'}, /* '+str(i)+' '+provenance[i]['key']+' */' for i,(b,u) in enumerate(mapping)]
    inc += ['};','']
    corrections=[]
    full_catalog=catalog['monsters']+sidecar['monsters']
    for p,d in zip(provenance,full_catalog):
        normal=cases[p['kind'],0,2,0]
        native=dict(hp_min=normal[0]//64,hp_max=cases[p['kind'],0,2,1][0]//64,
                    level=normal[1],xp=normal[2],hit=normal[3],armor=normal[4],
                    damage_min=normal[5],damage_max=normal[6],immune=normal[9]&7,resist=normal[9]>>3)
        changes={n:{'catalog':d[n],'native':v} for n,v in native.items() if d[n]!=v}
        if changes:corrections.append(dict(kind=p['kind'],key=p['key'],changes=changes))
    report=dict(source_revision=REVISION,source_sha256=PINNED,build='retail, HELLFIRE/SPAWN undefined',
                native_field_widths={**WIDTH_PROVENANCE,'excerpt_path':WIDTH_SOURCE,'excerpt_sha256':WIDTH_SOURCE_SHA},
                kinds=len(mapping),base_records=len(bases),unique_records=len(uniques),
                runtime_bytes=16*len(bases)+8*len(uniques)+2*len(mapping),
                source_c_oracle_cases=len(cases),source_c_oracle_sha256=oracle_hash,
                manual_normal_unique_boundaries=boundaries,
                max_hp=(max(c[0] for c in cases.values())+63)//64,
                max_xp=max(c[2] for c in cases.values()),max_hit=max(c[3] for c in cases.values()),
                catalog_corrections=corrections,mapping=provenance,
                preserved_catalog_sha256={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in (catalog_path,sidecar_path)},
                artwork_written=False)
    outputs={'monster_difficulty.h':header,'monster_difficulty.inc':'\n'.join(inc),
             'monster_difficulty-report.json':json.dumps(report,indent=2)+'\n'}
    for name,data in outputs.items():
        path=ROOT/'generated'/name
        if args.check:assert path.read_text()==data,name+' differs'
        else:path.write_text(data)
    print(f'PASS {len(mapping)} kinds, {len(bases)} base / {len(uniques)} unique records, {report["runtime_bytes"]} bytes; {len(cases)} compiled native cases and manual boundaries')


if __name__=='__main__':
    main()
