#!/usr/bin/env python3
"""Inspect real ARM O2 function stack frames and emitted direct call edges.

The report is an acyclic call-graph estimate, NOT a proven maximum. Recursive
edges are reported and omitted from sums. Branch conditions, indirect calls and
compiler-runtime helper frames need separate review. Parsing ELF symbols plus
CALL/JUMP24 relocations and assembler-resolved local branches preserves ARM
noinline/tail-call decisions. -fstack-size-section is used because Zig 0.13 does
not reliably retain -fstack-usage sidecar files.

Read existing objects:
    python3 tools/stack_audit.py --objects-dir /scratch/arm-with-stack-info
Optional compile-only reproduction (no link or ROM):
    python3 tools/stack_audit.py --objects-dir /scratch/stack --zig /path/to/zig
"""
from pathlib import Path
import argparse,collections,hashlib,json,struct,subprocess

PROJECT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description="ARM function frames and direct call graph; recursion is explicitly not bounded")
parser.add_argument('--objects-dir',type=Path,required=True,help='directory for ARM objects containing .stack_sizes')
parser.add_argument('--zig',help='optionally compile current production C objects with this Zig before analysis; no link/ROM')
parser.add_argument('--report',type=Path,help='default: OBJECTS_DIR/stack-report.json')
args=parser.parse_args()
ROOT=args.objects_dir.resolve()
if args.zig:
 ROOT.mkdir(parents=True,exist_ok=True)
 flags=['-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-marm','-O2','-g','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-Wall','-Wextra','-Werror','-Wno-unused-command-line-argument','-DGRAPHICS_ASSETS','-fstack-size-section']
 names='game controller arrows progress spells items shop quests monster_ai objects exploration lz actor_frames flash link coop coop_wire dungeon dungeon_visual world worldgen/runtime worldgen/l2 worldgen/l3 worldgen/l4 potions audio gba_mode0'.split()
 for name in names:
  extra=['-Wno-logical-op-parentheses','-Wno-switch-bool'] if name.startswith('worldgen/') else []
  subprocess.run([args.zig,'cc',*flags,*extra,'-c',str(PROJECT/'src'/(name+'.c')),'-o',str(ROOT/(Path(name).name+'.o'))],check=True)
class Elf:
 def __init__(self,path):
  self.path=path; self.data=d=path.read_bytes();assert d[:6]==b'\x7fELF\x01\x01'
  off=struct.unpack_from('<I',d,32)[0];size,count,strings=struct.unpack_from('<HHH',d,46)
  self.sections=[dict(zip(('nameoff','type','flags','address','offset','size','link','info','align','entsize'),struct.unpack_from('<10I',d,off+i*size))) for i in range(count)]
  names=self.bytes(self.sections[strings])
  for s in self.sections:s['name']=self.string(names,s['nameoff'])
  self.symbols=[]
  for s in self.sections:
   if s['type']!=2:continue
   strings=self.bytes(self.sections[s['link']])
   for p in range(s['offset'],s['offset']+s['size'],s['entsize']):
    name,value,size,info,other,index=struct.unpack_from('<IIIBBH',d,p)
    self.symbols.append(dict(name=self.string(strings,name),value=value,size=size,type=info&15,bind=info>>4,index=index))
  self.funcs=[s for s in self.symbols if s['type']==2 and s['index'] and s['size']]
 def string(self,d,i):return d[i:d.find(b'\0',i)].decode()
 def bytes(self,s):return self.data[s['offset']:s['offset']+s['size']]
 def relocs(self):
  for s in self.sections:
   if s['type']!=9:continue
   for p in range(s['offset'],s['offset']+s['size'],8):
    offset,info=struct.unpack_from('<II',self.data,p)
    yield s['info'],offset,info&255,self.symbols[info>>8]
 def func_at(self,section,offset):return next((f for f in self.funcs if f['index']==section and f['value']<=offset<f['value']+f['size']),None)
 def key(self,f):return self.path.stem+'::'+f['name'] if not f['bind'] else f['name']

elves=[Elf(p) for p in sorted(ROOT.glob('*.o'))]
assert elves, 'No ARM objects found'
assert any(s['name']=='.stack_sizes' for e in elves for s in e.sections), 'Compile objects with -fstack-size-section first'
functions={}
for e in elves:
 for f in e.funcs:
  key=e.key(f);assert key not in functions,key
  functions[key]={'file':e.path.name,'name':f['name'],'size':f['size'],'frame':None,'calls':[],'indirect_calls':[]}
 for sec,off,typ,sym in e.relocs():
  target=e.sections[sec]
  if target['name']=='.stack_sizes':
   assert typ==2
   data=e.bytes(target);addr=struct.unpack_from('<I',data,off)[0]+sym['value'];f=e.func_at(sym['index'],addr);assert f and addr==f['value']
   i=off+4;frame=0;shift=0
   while True:
    b=data[i];i+=1;frame|=(b&127)<<shift;shift+=7
    if not b&128:break
   functions[e.key(f)]['frame']=frame
 for sec,off,typ,sym in e.relocs():
  if typ not in (28,29):continue
  f=e.func_at(sec,off)
  if not f:continue
  dest=e.key(sym) if sym['index'] else sym['name']
  if sym['type']==3:
   # A branch reloc against a section is unusual; resolve its ARM addend.
   ins=struct.unpack_from('<I',e.data,e.sections[sec]['offset']+off)[0]
   imm=ins&0xffffff;imm=imm-(1<<24) if imm&(1<<23) else imm
   called=e.func_at(sym['index'],sym['value']+imm*4+8)
   dest=e.key(called) if called else '<unresolved section branch>'
  # Conditional BL uses R_ARM_JUMP24 too. The relocation alone cannot tell
  # a tail branch from a call; inspect ARM's link bit to retain its frame.
  branch=struct.unpack_from('<I',e.data,e.sections[sec]['offset']+off)[0]
  functions[e.key(f)]['calls'].append({'target':dest,'tail':not bool(branch&(1<<24)),'offset':off})
 relocated={(sec,off) for sec,off,typ,sym in e.relocs() if typ in (28,29)}
 mappings=collections.defaultdict(list)
 for sym in e.symbols:
  if sym['name'].startswith(('$a.','$d.','$t.')):mappings[sym['index']].append((sym['value'],sym['name'][1]))
 for values in mappings.values():values.sort()
 for f in e.funcs:
  sec=e.sections[f['index']];data=e.bytes(sec)
  for off in range(f['value'],f['value']+f['size'],4):
   if (f['index'],off) in relocated:continue
   mode=next((kind for pos,kind in reversed(mappings[f['index']]) if pos<=off),'a')
   if mode!='a':continue
   ins=struct.unpack_from('<I',data,off)[0]
   if ins&0x0e000000!=0x0a000000:continue
   imm=ins&0xffffff;imm=imm-(1<<24) if imm&(1<<23) else imm
   dest=off+8+imm*4;called=e.func_at(f['index'],dest)
   if not called or called['value']!=dest:continue
   link=bool(ins&(1<<24))
   if called==f and not link:continue
   functions[e.key(f)]['calls'].append({'target':e.key(called),'tail':not link,'offset':off,'resolved_branch':True})
 # ARM7 compiler uses mov lr,pc; bx reg for an indirect call (BLX is v5).
 for f in e.funcs:
  sec=e.sections[f['index']];data=e.bytes(sec)
  for off in range(f['value']+4,f['value']+f['size'],4):
   prev,ins=struct.unpack_from('<II',data,off-4)
   if ins&0x0ffffff0==0x012fff10 and ins&15!=14 and prev&0x0fffffff==0x01a0e00f:
    functions[e.key(f)]['indirect_calls'].append({'offset':off,'register':ins&15})

# Eliminate duplicate call edges before exploring. Keep the non-tail edge if both exist.
for f in functions.values():
 calls={}
 for c in f['calls']:
  if c['target'] not in calls or not c['tail']:calls[c['target']]=c
 f['calls']=list(calls.values())

# A small memoized SCC-free path solver is used for reporting; cycles are recorded,
# not assigned an invented depth. Source review must bound every reachable cycle.
index=0;stack=[];indices={};low={};on=set();components=[]
def strong(v):
 global index
 indices[v]=low[v]=index;index+=1;stack.append(v);on.add(v)
 for edge in functions[v]['calls']:
  w=edge['target']
  if w not in functions:continue
  if w not in indices:strong(w);low[v]=min(low[v],low[w])
  elif w in on:low[v]=min(low[v],indices[w])
 if low[v]==indices[v]:
  s=[]
  while True:
   w=stack.pop();on.remove(w);s.append(w)
   if w==v:break
  components.append(s)
for v in functions:
 if v not in indices:strong(v)
recursive=[s for s in components if len(s)>1 or any(c['target']==s[0] for c in functions[s[0]]['calls'])]
recursive_edges={(a,c['target']) for s in recursive for a in s for c in functions[a]['calls'] if c['target'] in s}
memo={}
def longest(v):
 if v not in functions:return 0,[{'name':v,'frame':None,'external':True}]
 if v in memo:return memo[v]
 f=functions[v];frame=f['frame'] or 0;best=frame;path=[{'name':v,'frame':f['frame']}]
 for c in f['calls']:
  if (v,c['target']) in recursive_edges:continue
  n,p=longest(c['target']);total=max(frame,n) if c['tail'] else frame+n
  if total>best:best=total;path=[{'name':v,'frame':f['frame'],'tail_to_next':c['tail']}]+p
 memo[v]=(best,path);return memo[v]
names=['main','singleplayer_run_frame','ending_enter','frontend_enter','coop_enter','coop_campaign_enter','campaign_transaction','campaign_stage','coop_run_frame','coop_campaign_travel','coop_leave','coop_save_session','coop_campaign_host_save','coop_campaign_personal_save','save_restore','game_generate_dungeon','worldgen_l2','worldgen_l3','worldgen_l4','audio_vblank_irq']
paths={}
for name in names:
 keys=[k for k,f in functions.items() if f['name']==name]
 for key in keys:
  size,path=longest(key);paths[key]={'acyclic_direct_estimate_bytes':size,'path':path}
report={'functions':functions,'recursive_components':recursive,'paths':paths,'missing_frames':[k for k,v in functions.items() if v['frame'] is None], 'external_targets':sorted({c['target'] for f in functions.values() for c in f['calls'] if c['target'] not in functions}),'indirect_calls':{k:v['indirect_calls'] for k,v in functions.items() if v['indirect_calls']},'limitations':['Recursive SCC edges omitted from displayed acyclic sums, no bounded maximum claimed.','Indirect target calls and external compiler runtime frames are not included.','Function frame maxima conservatively retained across direct calls; tail jump edges use max rather than addition.','This analysis does not measure a linked ROM, emulator, hardware, or stack high-water mark.']}
report['object_sha256']={e.path.name:hashlib.sha256(e.data).hexdigest() for e in elves}
report_path=args.report or ROOT/'stack-report.json'
report_path.write_text(json.dumps(report,indent=2)+'\n')
for k in paths:
 print(k,paths[k]['acyclic_direct_estimate_bytes'],' -> '.join(f"{p['name']}({p['frame']})" for p in paths[k]['path']))
print('RECURSIVE',json.dumps(recursive));print('MISSING',report['missing_frames']);print('EXTERNAL',report['external_targets']);print('INDIRECT',report['indirect_calls'])
print('LARGEST',sorted(((v['frame'] or 0,k) for k,v in functions.items()),reverse=True)[:30])
