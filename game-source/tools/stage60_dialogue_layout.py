#!/usr/bin/env python3
"""Execute production proportional wrapper and independently validate all retail prose."""
import argparse,json,re,subprocess,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('project',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
s=(a.source/'src/town_dialogue.inc').read_text()
def function(name):
 start=re.search(r'static UI_FN [^\n]+\b'+name+r'\(',s).start();body=s.index('{',start);depth=1;end=body+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
font_text=(a.project/'generated/town_dialogue_font.h').read_text();font=[[int(v,16) for v in row.split(',')] for row in re.findall(r'\{((?:0x[0-9a-f]+,?)+)\}',font_text)]
assert len(font)==96 and all(len(r)==8 for r in font)
def metric(ch):
 rows=font[ord(ch)-32] if 32<=ord(ch)<128 else font[ord('?')-32]
 xs=[x for x in range(8) if any(r&(128>>x) for r in rows)]
 return (min(xs),max(xs)-min(xs)+1) if xs else (0,3)
def width(t):return sum(metric(c)[1]+1 for c in t)-bool(t)
records=json.loads((a.project/'source_assets/town_dialogue_stage53/town-dialogue-catalog.json').read_text())['dialogues']; texts=[r['text'].rstrip().removesuffix('|').rstrip() for r in records]
texts+=['W'*200,"  I.M! I, W \n\n A  B\r C",' \r\n','!.,;:']
code='#include <stdio.h>\n#include <stdint.h>\n#define UI_FN\nenum{NPC_WIDTH=224};\n#include "'+str(a.project/'generated/town_dialogue_font.h')+'"\n'+function('dialogue_glyph')+'\n'+function('dialogue_line')+'\nstatic const char *texts[]={'+','.join(json.dumps(t) for t in texts)+'};\nint main(void){for(unsigned i=0;i<sizeof(texts)/sizeof(texts[0]);i++){const char *s=texts[i];while(*s){const char *start=s;while(*start==\' \'||*start==\'\\r\')start++;unsigned n;const char*next=dialogue_line(s,&n);printf("%u,%u,%u,%u\\n",i,(unsigned)(start-texts[i]),n,(unsigned)(next-texts[i]));if(next<=s)return 2;s=next;}}}\n'
(a.output/'probe.c').write_text(code);subprocess.run(['gcc','-std=c11','-O1','-fsanitize=address,undefined','-g',str(a.output/'probe.c'),'-o',str(a.output/'probe')],check=True)
output=subprocess.check_output([str(a.output/'probe')],text=True,env={**os.environ,'ASAN_OPTIONS':os.environ.get('ASAN_OPTIONS','detect_leaks=0')});lines=[[] for _ in texts]
for row in output.splitlines():
 i,start,n,end=map(int,row.split(','));text=texts[i][start:start+n];assert width(text)<=224,(i,text,width(text));assert n or texts[i][start] in '\n\r';assert end>start or not n
 lines[i].append(text)
for text,wrapped in zip(texts,lines):assert ''.join(text.split())==''.join(''.join(wrapped).split())
manifest={'result':'PASS','messages':len(records),'stress_cases':4,'glyphs':96,'max_width':224,'line_pitch':10,'blank_rows':3,'line_count':sum(map(len,lines)),'records':[{ 'speech_id':r['speech_id'],'speech':r['speech'],'lines':ls,'scroll_positions':max(1,len(ls)-9)} for r,ls in zip(records,lines)],'font':font,'metrics':[metric(chr(i)) for i in range(32,128)]}
(a.output/'report.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k not in ('records','font','metrics')}))
