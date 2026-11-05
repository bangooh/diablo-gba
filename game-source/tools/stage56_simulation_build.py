#!/usr/bin/env python3
"""Reproduce the Stage56 isolated ARM simulation variants and Timer0 profiles.
The baseline directory must contain frozen src/, all linked .o files, and the
matching diablo-gba-assets.gba. All writes stay under --work. Profile builds
contain temporary counters and are diagnostic artifacts, not deliverable ROMs.
"""
from pathlib import Path
import shutil,subprocess,struct,json,hashlib,os,sys
import argparse
parser=argparse.ArgumentParser(description='Build an isolated simulation candidate/profile against frozen GBA object files; never modifies the baseline or production build.')
parser.add_argument('--baseline',type=Path,required=True)
parser.add_argument('--work',type=Path,required=True)
parser.add_argument('--candidate',type=Path)
parser.add_argument('--profile',action='store_true')
parser.add_argument('--label',default='candidate')
parser.add_argument('--zig',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];base=args.baseline.resolve();work=args.work.resolve()
assert work!=base and not work.is_relative_to(base) and not work.is_relative_to(root/'build')
assert (base/'src/game.c').is_file() and (base/'assets.o').is_file()
work.mkdir(parents=True,exist_ok=True)
variant=args.label;assert variant.replace('-','').replace('_','').isalnum()
candidate=args.candidate.resolve() if args.candidate else base/'src/monster_ai.c'
(work/'generated').mkdir(exist_ok=True)
for file in [*(root/'generated').glob('*.h'),*(root/'generated').glob('*.inc')]:shutil.copy2(file,work/'generated'/file.name)
if not (work/'src').exists():shutil.copytree(base/'src',work/'src')
header='''#include <stdint.h>
extern volatile unsigned stage56_profile_ticks[16],stage56_profile_calls[16];
#define P_TIME (*(volatile uint16_t*)0x04000100)
#define P_START(name) unsigned name=P_TIME
#define P_END(name,index) do{stage56_profile_ticks[index]+=(uint16_t)(P_TIME-name);stage56_profile_calls[index]++;}while(0)
#define P_CALL(index,statement) do{P_START(p_at);statement;P_END(p_at,index);}while(0)
'''
(work/'src/stage56_profile.h').write_text(header)
if args.profile:
 g=(base/'src/game.c').read_text();g='#include "stage56_profile.h"\nvolatile unsigned stage56_profile_ticks[16],stage56_profile_calls[16];\n'+g
 g=g.replace('void game_step(Game *g,unsigned held,unsigned pressed) {','static void game_step_inner(Game *g,unsigned held,unsigned pressed) {',1)
 # Only instrument the public step body, leaving helper occurrences alone.
 begin=g.index('static void game_step_inner(');a=g[:begin];s=g[begin:]
 for stmt,index in [('game_quest_npc_step(g);',1),('game_item_effects_step(g);',2),('game_quest_enter(g);game_props_step(g);',3),('game_activate_nearby(g);',4),('game_arrows_step(g);',6),('game_monsters_step(g);',7)]:
  assert stmt in s;s=s.replace(stmt,'P_CALL('+str(index)+','+stmt+');',1)
 s=s.replace('int hit=game_controller_step(g,held,pressed);','int hit;P_CALL(5,hit=game_controller_step(g,held,pressed));',1)
 g=a+s+'\nvoid game_step(Game *g,unsigned held,unsigned pressed){P_CALL(0,game_step_inner(g,held,pressed));}\n'
 (work/'src/game.c').write_text(g)
 m=candidate.read_text();m='#include "stage56_profile.h"\n'+m
 begin=m.index('void game_monsters_step(');a=m[:begin];s=m[begin:]
 s=s.replace('    hint_x=g->x-OCCUPANCY_SIDE/2;','    P_START(p_scratch);\n    hint_x=g->x-OCCUPANCY_SIDE/2;',1)
 s=s.replace('    for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1){','    P_END(p_scratch,8);P_START(p_occupancy);\n    for(int i=0;i<MAX_ENEMIES;i++)if(g->enemies[i].alive==1){',1)
 s=s.replace('    game_corpses_step();','    P_END(p_occupancy,9);P_CALL(10,game_corpses_step());P_START(p_ai);',1)
 s=s.replace('    occupied_hint=0;','    P_END(p_ai,11);\n    occupied_hint=0;',1)
 (work/'src/monster_ai.c').write_text(a+s)
else:
 shutil.copy2(base/'src/game.c',work/'src/game.c')
 shutil.copy2(candidate,work/'src/monster_ai.c')
zig=args.zig
flags=['-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-marm','-O2','-g','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-Wall','-Wextra','-Werror','-Wno-unused-command-line-argument','-DGRAPHICS_ASSETS']
out=work/variant;out.mkdir(exist_ok=True)
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(work/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(work/'zig-local'))
for name in ['game','monster_ai']:subprocess.run([zig,'cc',*flags,'-c',str(work/'src'/f'{name}.c'),'-o',str(out/f'{name}.o')],check=True,env=env)
objects=[str(out/p.name if p.stem in ['game','monster_ai'] else p) for p in base.glob('*.o')]
link=(root/'link.ld').read_text();(work/'profile-link.ld').write_text(link.replace('LENGTH = 24K','LENGTH = 25K') if args.profile else link)
elf=out/'diablo-gba-assets.elf';subprocess.run([zig,'cc',*flags,'-nostdlib',*objects,'-Wl,-T,'+str(work/'profile-link.ld'),'-o',str(elf)],check=True,env=env)
data=elf.read_bytes();phoff=struct.unpack_from('<I',data,28)[0];phsize,phcount=struct.unpack_from('<HH',data,42);rom=bytearray();iwram=ewram=0
for i in range(phcount):
 kind,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',data,phoff+i*phsize)
 if kind!=1:continue
 if 0x02000000<=va<0x02040000:ewram+=ms
 if 0x03000000<=va<0x03008000:iwram+=ms
 if fs and 0x08000000<=pa<0x0a000000:
  dst=pa-0x08000000
  if len(rom)<dst+fs:rom.extend(bytes(dst+fs-len(rom)))
  rom[dst:dst+fs]=data[off:off+fs]
rom[4:192]=(base/'diablo-gba-assets.gba').read_bytes()[4:192];used=len(rom);rom.extend(b'\xff'*(33554432-len(rom)));(out/'diablo-gba-assets.gba').write_bytes(rom)
report={'profileOnly':args.profile,'baselineRomSha256':hashlib.sha256((base/'diablo-gba-assets.gba').read_bytes()).hexdigest(),'monsterSourceSha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),'sha256':hashlib.sha256(rom).hexdigest(),'rom_used':used,'iwram':iwram,'ewram':ewram};(out/'build.json').write_text(json.dumps(report,indent=2));print(variant,report)
