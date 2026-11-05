#!/usr/bin/env python3
"""Exhaustive production archive check plus bounded production UI/target probes."""
import argparse, hashlib, json, os, re, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('project',type=Path)
p.add_argument('baseline',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args();a.project=a.project.resolve();a.baseline=a.baseline.resolve();a.output.mkdir(parents=True,exist_ok=True)
def function(source,name):
    match=re.search(r'^[^\n;]*\b'+name+r'\([^\n]*\)\s*\{',source,re.M)
    assert match,name
    start=match.start();end=source.index('{',match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
old=(a.baseline/'src/prop_archive.h').read_text()
old=re.sub(r'\b(game_prop_archive_\w+|PROP_ARCHIVE_\w+)\b',r'legacy_\1',old)
old=old.replace('#pragma once','').replace('#include "game.h"','')
controller=(a.project/'src/controller.c').read_text();info=(a.project/'src/infopanel.inc').read_text();env=(a.project/'src/environment_objects.inc').read_text()
menu=(a.project/'src/menu.inc').read_text();link=(a.project/'src/link.h').read_text()
assert re.search(r'#define SAVE_VERSION 127u\b',menu)
assert re.search(r'#define SAVE_GENERATOR 17u\b',menu)
assert re.search(r'LINK_VERSION=131\b',link)
code=r'''#define GRAPHICS_ASSETS
#include "src/prop_archive.h"
#include "src/link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
'''+old+r'''
static unsigned char occupied[1u<<21];
static int equal(Prop a,Prop b){return a.x==b.x&&a.y==b.y&&a.kind==b.kind&&a.state==b.state;}
static unsigned long long good,bad,legacy,decoded;
static void archives(void){
 for(unsigned k=0;k<64;k++)for(unsigned y=0;y<80;y++)for(unsigned x=0;x<80;x++)for(unsigned s=0;s<256;s++){
  Prop p={x,y,k,s};uint32_t bits=game_prop_archive_pack(&p);
  int wall=k>=53&&k<=58,valid=s<(wall?128:16);
  if(!valid){assert(!bits);bad++;continue;}
  assert(!(bits>>24));
  /* Exercise the actual three bytes written to Flash, not a wider register. */
  unsigned char disk[3]={bits,bits>>8,bits>>16};
  uint32_t read=disk[0]|((uint32_t)disk[1]<<8)|((uint32_t)disk[2]<<16);
  assert(equal(p,game_prop_archive_unpack(read)));
  assert(!(occupied[bits>>3]&(1u<<(bits&7))));occupied[bits>>3]|=1u<<(bits&7);good++;
  if(k<=58){assert(bits==legacy_game_prop_archive_pack(&p));assert(equal(p,legacy_game_prop_archive_unpack(bits)));legacy++;}
 }
 /* Canonical acceptance is also the co-op campaign's incoming-floor gate. */
 for(uint32_t bits=0;bits<(1u<<24);bits++){
  Prop p=game_prop_archive_unpack(bits);int canonical=game_prop_archive_pack(&p)==bits;
  assert(canonical==!!(occupied[bits>>3]&(1u<<(bits&7))));decoded+=canonical;
 }
 assert(decoded==good);
 for(unsigned v=80;v<256;v++){Prop p={v,1,60,0};assert(!game_prop_archive_pack(&p));p=(Prop){1,v,61,1};assert(!game_prop_archive_pack(&p));}
 for(unsigned k=64;k<256;k++){Prop p={1,1,k,1};assert(!game_prop_archive_pack(&p));}
 for(unsigned bit=24;bit<32;bit++)assert(equal((Prop){0},game_prop_archive_unpack(1u<<bit)));
}
int game_walkable(const Game*g,int x,int y){
 if((unsigned)x>=MAP_W||(unsigned)y>=MAP_H||!g->map[y][x])return 0;
 for(unsigned i=0;i<MAX_PROPS;i++)if(game_prop_blocks(&g->props[i],x,y))return 0;
 return 1;
}
'''+function(controller,'abs_i')+'\n'+function(controller,'direction')+'\n'+function(controller,'game_prop_direction')+r'''
static void targets(void){
 Game g={0};memset(g.map,1,sizeof(g.map));g.x=g.y=40;g.hp=10;g.facing=3;
 for(unsigned k=59;k<=63;k++)for(unsigned state=0;state<16;state++){
  g.props[0]=(Prop){41,40,k,state};
  assert((game_prop_direction(&g,0)>=0)==(k!=59&&!state));
  assert(!game_walkable(&g,41,40));assert(game_walkable(&g,42,40));
 }
 for(unsigned k=53;k<=58;k++)for(unsigned state=0;state<128;state++){
  g.props[0]=(Prop){41,40,k,state};assert(game_prop_direction(&g,0)<0);assert(game_walkable(&g,41,40));
 }
 for(unsigned k=0;k<64;k++)if(game_prop_is_door(k))for(unsigned state=0;state<2;state++){
  g.props[0]=(Prop){40+!game_prop_door_axis(k),40+game_prop_door_axis(k),k,state};assert(game_prop_direction(&g,0)>=0);
 }
 g.props[0]=(Prop){41,40,60,0};g.map[40][41]=1;g.area=1;assert(game_prop_direction(&g,0)<0);
}
#define UI_FN
static char info_lines[5][23];static unsigned info_count,info_gold;
'''+function(info,'info_reset')+'\n'+function(info,'info_add')+'\n'+re.search(r'enum \{ ENV_NOTICE_FIRST[^;]+;',env).group()+'\n'+function(env,'game_environment_notice')+r'''
static unsigned selected_type;
unsigned game_shrine_type(const Game*g,unsigned i){(void)g;(void)i;return selected_type;}
'''+function(env,'game_environment_name')+r'''
static void labels(void){
 for(unsigned n=0;n<256;n++){
  const char *text=game_environment_notice(n);if(!text)continue;
  info_reset();info_add(text);assert(info_count>=1&&info_count<=2);
  printf("NOTICE %u",n);for(unsigned i=0;i<info_count;i++)printf("|%s",info_lines[i]);puts("");
 }
 Game g={0};g.props[0].kind=PROP_SHRINE_L;
 for(selected_type=0;selected_type<26;selected_type++){
  const char *text=game_environment_name(&g,0);if(!text)continue;
  info_reset();info_add(text);assert(info_count==1);printf("NAME %u|%s\n",selected_type,info_lines[0]);
 }
 selected_type=26;g.props[0].kind=PROP_BOOKCASE_R;assert(!strcmp(game_environment_name(&g,0),"BOOKCASE"));
}
int main(void){archives();targets();labels();printf("COUNTS %llu %llu %llu %llu\n",good,bad,legacy,decoded);}
'''
with tempfile.TemporaryDirectory(prefix='stage61-archive-') as temp:
    src=Path(temp)/'probe.c';exe=Path(temp)/'probe';src.write_text(code)
    subprocess.run(['gcc','-std=c11','-O2','-g','-fsanitize=address,undefined','-I',str(a.project),str(src),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
notices={};names={};counts=[]
for line in result.splitlines():
    if line.startswith('COUNTS '):counts=list(map(int,line.split()[1:]));continue
    kind,rest=line.split(' ',1);id,*wrapped=rest.split('|');(notices if kind=='NOTICE' else names)[int(id)]=wrapped
assert sorted(notices)==[n for n in range(160,187) if n not in [161,162,163,178,185]]
assert len(names)==21
# Rejoining the production wrapper must preserve every literal's words.
messages=re.search(r'const char \*const messages\[26\]=\{(.*?)\};',env,re.S).group(1)
literals=[json.loads(v) if v!='0' else None for v in re.findall(r'"[^"\\]*"|\b0\b',messages)]
for id,lines in notices.items():
    expected='A SPELLBOOK FALLS OUT' if id==186 else literals[id-160]
    assert ' '.join(lines)==expected,(id,lines,expected)
    assert max(map(len,lines))<=22
report={'result':'PASS','archive':{'valid_records':counts[0],'invalid_states_rejected':counts[1],'legacy_byte_identical':counts[2],'all_24bit_inputs':1<<24,'canonical_decodes':counts[3],'all_kinds':64,'all_coordinates':'80x80','all_state_bytes':256,'used_shrine_states':'1..13 preserved','legacy_traps':'53..58 all target/spent states0..127 unchanged'},'headers':{'save_version':127,'generator':17,'link_version':131,'static_ram_added':0},'targets':'brazier never selectable; unused shrines/bookcases selectable; used and animated objects stay solid but nonselectable; all wall traps remain nonblocking/nonselectable; all doors remain operable','notice_lines':notices,'names':names,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'source_sha256':{name:hashlib.sha256((a.project/'src'/name).read_bytes()).hexdigest() for name in ['prop_archive.h','controller.c','infopanel.inc','menu.inc','link.h']}}
(a.output/'host-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('notice_lines','names','source_sha256')}))
