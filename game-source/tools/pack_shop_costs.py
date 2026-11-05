"""Import retail spell book/staff prices from pinned spelldat.cpp."""
from pathlib import Path
import re,subprocess
r=Path(__file__).resolve().parents[1]
s=(r/'source_assets/item_catalog/spelldat.cpp').read_text().replace('#include "all.h"','')
s=subprocess.run(['gcc','-E','-P','-x','c','-'],input=s,text=True,capture_output=True,check=True).stdout
rows=[]
for line in s.splitlines():
 if not re.match(r'\s*\{ SPL_',line):continue
 line=re.sub(r'\{[^{}]*\}', 'MISSILES',line)
 a=[v.strip() for v in line.strip().lstrip('{').rstrip(',').rstrip('}').split(',')]
 rows.append((int(a[-2]),int(a[-1])))
assert len(rows)==37
(r/'generated/shop_spell_costs.h').write_text('/* Pinned retail spelldat.cpp: book cost, staff cost. */\nstatic const uint16_t shop_spell_cost[37][2]={\n'+''.join(' {%d,%d},\n'%v for v in rows)+'};\n')
