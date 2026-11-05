"""Import retail affix/unique tables without Hellfire; preserve every source field.
Runtime only enables powers whose gameplay consumers are implemented.
"""
from pathlib import Path
import argparse, json, hashlib, re, subprocess, tempfile
R=Path(__file__).resolve().parents[1]; S=R/'source_assets/item_catalog'
AFF='name power param1 param2 min_level types alignment double_weight good min_value max_value multiplier'.split()
UNI='name base min_level count value'.split()+[x for i in range(6) for x in (f'power{i}',f'param{i}a',f'param{i}b')]
SUPPORTED=set(range(12))|set(range(19,40))|{14,15,16,17,42,43,45,46,57,53,55,56,58,59,60,61} # Numeric combat, attributes, life/mana and resistances.
p=argparse.ArgumentParser();p.add_argument('--extract',action='store_true');args=p.parse_args()
if args.extract:
    source=(S/'itemdat.cpp').read_text();enums=(S/'enums.h').read_text()
    code='#include <stdio.h>\n#include <stddef.h>\n#define TRUE 1\n#define FALSE 0\n#define PAL16_BLUE 176\n#define PAL16_RED 224\n#define PAL16_YELLOW 192\n#include "'+str(S/'enums.h')+'"\n'
    for typename,fields in [('PLStruct',AFF),('UItemStruct',UNI)]:
        code+='typedef struct {'+';'.join(('const char *' if f=='name' else 'int ')+f for f in fields)+';} '+typename+';\n'
    tables=[('PL_Prefix',AFF),('PL_Suffix',AFF),('UniqueItemList',UNI)]
    for name,_ in tables:code+=re.search(r'const \w+ '+name+r'\[\] = \{.*?\n\};',source,re.S)[0]+'\n'
    code+='int main(){\n'
    for name,fields in tables:
        code+='for(auto &r:'+name+'){if(!r.name||!*r.name)break;printf("'+name+'\\t%s",r.name);'
        for f in fields[1:]:code+='printf("\\t%d",r.'+f+');'
        code+='puts("");}\n'
    code+='}'
    with tempfile.TemporaryDirectory() as td:
        cpp=Path(td)/'dump.cpp';exe=Path(td)/'dump';cpp.write_text(code)
        subprocess.run(['g++','-std=c++11',str(cpp),'-o',str(exe)],check=True)
        output=subprocess.check_output([str(exe)],text=True)
    data={'revision':'9f01757243412bb154d02d9af65c804159e6e0ea','variant':'Diablo retail; HELLFIRE and SPAWN undefined','source_sha256':{n:hashlib.sha256((S/n).read_bytes()).hexdigest() for n in ['itemdat.cpp','enums.h']}}
    for name,fields in tables:
        rows=[]
        for line in output.splitlines():
            parts=line.split('\t')
            if parts[0]!=name:continue
            assert len(parts)==len(fields)+1
            rows.append({'source_id':len(rows),**{f:v if f=='name' else int(v) for f,v in zip(fields,parts[1:])}})
        data[name]=rows
    (S/'magic_items.json').write_text(json.dumps(data,indent=2)+'\n')
data=json.loads((S/'magic_items.json').read_text())
for n,h in data['source_sha256'].items():assert hashlib.sha256((S/n).read_bytes()).hexdigest()==h
pre,suf,unique=[data[n] for n in ['PL_Prefix','PL_Suffix','UniqueItemList']]
assert len(pre)<128 and len(suf)<128
header='''#pragma once
#include <stdint.h>
typedef struct { const char *name; int32_t types; int16_t power,param1,param2; int32_t min_value,max_value; int16_t multiplier; uint8_t min_level,alignment,double_weight,good; } ItemAffix;
typedef struct { int16_t power,low,high; } ItemPower;
typedef struct { const char *name; int32_t value; uint8_t base,min_level,count,reserved; ItemPower powers[6]; } UniqueItemDef;
'''
header+=f'#define ITEM_PREFIX_COUNT {len(pre)}\n#define ITEM_SUFFIX_COUNT {len(suf)}\n#define ITEM_UNIQUE_COUNT {len(unique)}\n'
header+='extern const ItemAffix item_prefixes[ITEM_PREFIX_COUNT],item_suffixes[ITEM_SUFFIX_COUNT];\nextern const UniqueItemDef item_uniques[ITEM_UNIQUE_COUNT];\n'
(R/'src/item_magic_catalog.h').write_text(header)
out='/* Generated from the pinned retail tables. Do not edit. */\n'
for name,rows in [('item_prefixes',pre),('item_suffixes',suf)]:
    out+='const ItemAffix '+name+'[]={\n'
    for d in rows:
        vals=[json.dumps(d['name'])]+[str(d[f]) for f in ['types','power','param1','param2','min_value','max_value','multiplier','min_level','alignment','double_weight','good']]
        assert all(-32768<=d[f]<=32767 for f in ['power','param1','param2','multiplier'])
        out+=' {'+','.join(vals)+'},\n'
    out+='};\n'
out+='const UniqueItemDef item_uniques[]={\n'
for d in unique:
    out+=' {'+json.dumps(d['name'])+','+','.join(str(d[f]) for f in ['value','base','min_level','count'])+',0,{'+','.join('{'+','.join(str(d[f]) for f in [f'power{i}',f'param{i}a',f'param{i}b'])+'}' for i in range(6))+'}},\n'
out+='};\n'
(R/'generated/item_magic_catalog.inc').write_text(out)
report={'prefixes':len(pre),'suffixes':len(suf),'uniques':len(unique),'enabled_powers':sorted(SUPPORTED),'enabled_prefixes':[d['source_id'] for d in pre if d['power'] in SUPPORTED and d['types']&0x111111],'enabled_suffixes':[d['source_id'] for d in suf if d['power'] in SUPPORTED and d['types']&0x111111],'runtime_unique_generation':True,'enabled_unique_ids':[0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89],'unique_only_powers':[44,62,63,64,65,66,68,69,70,72,75,79],'source':data['source_sha256'],'limitations':['177 affixes enabled. The thieves suffix (half trap damage) awaits the trap system. Quest unique IDs1..9 await quest rewards, including random leech for the Undead Crown. Native 1.09 fixed-staff initial charges and melee-only demon damage quirks are preserved.','81 supported uniques enabled; 80 random candidates and the Butcher quest reward. Native highest eligible ID can shadow lower IDs. Per-item cursor dimensions determine bag occupancy.','Generation uses the GBA seed mixer, not the original PC RNG sequence.']}
(R/'generated/item-magic-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ['source','limitations']}))
