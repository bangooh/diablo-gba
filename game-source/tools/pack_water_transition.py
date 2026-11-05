"""Precompute native palette_update_quest_palette(32..1), no RGB interpolation."""
from pathlib import Path
import re,json,hashlib
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/quest_remaining'
h=(out/'water_quest.h').read_text()
def lut(name):return list(map(int,re.search(name+r'\[256\]=\{([^}]+)',h)[1].split(',')))
foul,clean=lut('water_foul_lut'),lut('water_clean_lut')
a=(src/'L3pfoul.pal').read_bytes();b=(src/'L3pwater.pal').read_bytes();assert a[96:]==b[96:]
rows=[clean[:i+1]+foul[i+1:] for i in range(32)]
assert rows[-1]==clean
assert all(row[32:]==clean[32:] for row in rows)
(out/'water_transition.h').write_text('#pragma once\nstatic const uint8_t water_transition_lut[32][32]={\n'+',\n'.join('{'+','.join(map(str,row[:32]))+'}' for row in rows)+'\n};\n')
report={'revision':'9f01757243412bb154d02d9af65c804159e6e0ea','function':'palette_update_quest_palette','arguments':list(range(32,0,-1)),'table_bytes':1024,'shared_tail_bytes_removed':7168,'source_sha256':{n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in ['palette.cpp','L3pfoul.pal','L3pwater.pal']}}
(out/'water-transition-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS generated 32 native palette-prefix stages, final stage equals clean LUT')
