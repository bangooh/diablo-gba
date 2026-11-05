"""Compare the27 implemented retail spell definitions against pinned spelldat."""
from pathlib import Path
import re,json
from pack_difficulty import fields
from stage53_dialogue_catalog import preprocess
R=Path(__file__).resolve().parents[1]
native=[fields(line[line.index('{')+1:line.rindex('}')]) for line in preprocess(R/'source_assets/item_catalog/spelldat.cpp').splitlines() if re.match(r'\s*\{\s*SPL_',line)]
text=(R/'src/spells.c').read_text().split('uint8_t spell_pending')[0]
rows={int(i):[x.strip() for x in t.split(',')] for i,t in re.findall(r'\[(\d+)\]=\{([^}]+)\}',text)}
names=['mana','mana_adjustment','minimum_mana','minimum_magic','book_floor','town_allowed','staff_floor','staff_minimum','staff_maximum'];report=[]
for i,row in rows.items():
 if not i:continue
 n=native[i];wanted=[int(n[j]) if n[j] not in ('TRUE','FALSE') else int(n[j]=='TRUE') for j in (1,12,13,9,5,8,6,14,15)]
 wanted[4]=max(0,wanted[4]);wanted[6]=max(0,wanted[6]);actual=[int(row[j]) for j in (1,2,3,4,5,7,8,9,10)]
 assert wanted==actual,(i,wanted,actual)
 report.append(dict(id=i,name=json.loads(row[0]),native_symbol=n[0],fields=dict(zip(names,actual))))
assert len(report)==27
(R/'generated/spell-source-stage71.json').write_text(json.dumps(dict(result='PASS',spells=report,comparisons=27*9,scope='Metadata parity, not full algorithmic equivalence; compact wave/persistent adaptations are documented separately.'),indent=2)+'\n')
print('PASS27 retail spell definitions /243 native field comparisons')
