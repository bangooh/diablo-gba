"""Dungeon quest prose from the same pinned retail textdat.cpp as town speech.
No voice-index changes; class-specific book variants follow AddBookLever.
"""
from pathlib import Path
import json,re
from stage53_dialogue_catalog import preprocess,body,enum,string,STRING,SOURCE
R=Path(__file__).resolve().parents[1]
e=enum(preprocess(SOURCE/'enums.h'),'_speech_id')
rows=re.findall(r'\{\s*((?:'+STRING+r'\s*)+),\s*(\d+),\s*(\d+),\s*(\w+)\s*\}',body(preprocess(SOURCE/'textdat.cpp'),'alltext[]'))
entries={7:('Book of Blood','BLOODY'),8:('Book of the Blind','BLINDING'),9:('Lachdanan','VEIL9'),10:('Lachdanan','VEIL11'),14:('Snotspill','BANNER10'),15:('Snotspill','BANNER12'),24:('Archbishop Lazarus','VILE13'),31:('Gharbad the Weak','GARBUD1'),32:('Gharbad the Weak','GARBUD2'),33:('Gharbad the Weak','GARBUD3'),34:('Gharbad the Weak','GARBUD4'),35:('Zhar the Mad','ZHAR1'),36:('Zhar the Mad','ZHAR2'),38:('Book of Bone','BONER'),39:('Steel Tome','BLOODWAR'),41:('Warlord of Blood','WARLRD9')}
lines=['#pragma once','/* Generated from retail source. Preserve prose; strip only terminal scroll delimiter. */','static const struct {const char *title,*text[3];} quest_prose71[42]={']
audit=[]
for n,(title,suffix) in entries.items():
 symbols=['TEXT_'+suffix]*3
 if suffix in ('BONER','BLOODY','BLINDING','BLOODWAR'):symbols=['TEXT_'+suffix,'TEXT_R'+suffix,'TEXT_M'+suffix]
 texts=[string(rows[e[s]][0]).rstrip().removesuffix('|').rstrip() for s in symbols]
 lines.append('['+str(n)+']={'+json.dumps(title)+',{'+','.join(json.dumps(t) for t in texts)+'}},')
 audit.append(dict(notice=n,title=title,symbols=symbols,texts=texts))
lines+=['};']
(R/'generated/quest_prose71.h').write_text('\n'.join(lines)+'\n')
(R/'generated/quest-prose-stage71.json').write_text(json.dumps(audit,indent=2)+'\n')
print('PASS',len(entries),'dungeon quest passages; original class-specific variants')
