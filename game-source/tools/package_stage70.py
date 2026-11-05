#!/usr/bin/env python3
"""Package completed Stage70 source/assets/evidence and final ROM/ELF.

Does not run or fabricate validation. Every release-bound input/evidence file
must still match the successful gate. Historical compiled outputs and caches
are omitted; original source assets, tools, tests and validation images remain.
"""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPORT=ROOT/'generated/stage70-release-report.json'
CURRENT_BUILD={'diablo-gba-assets.gba','diablo-gba-assets.elf','diablo-gba-assets-build-report.json'}
SKIP_DIRS={'__pycache__','.zig-cache','zig-cache','node_modules','.git'}
REQUIRED={'README.md','SPELL-GRAPHICS-STAGE70.md','src/item_drag.inc','src/spell_picker.inc','src/inventory.inc','src/actor_frames.c','src/spells.c','src/spell_effects.inc','src/gba_mode0.c','src/lighting.inc','src/death_sprites.inc','src/effect_tiles.h','src/effect_tiles.inc',
 'tools/pack_spell_icons.py','tools/stage70_native.mjs','tools/stage70_preview.py','tests/run-stage70-effects.py','tests/spell-separated-effects.c','generated/stage70-release-report.json',
 'build/diablo-gba-assets.gba','build/diablo-gba-assets.elf','build/diablo-gba-assets-build-report.json'}

VALIDATION_KEYS=()

def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def include(path):
 rel=path.relative_to(ROOT)
 if any(part in SKIP_DIRS for part in rel.parts):return False
 if rel.parts[0]=='build':return len(rel.parts)==2 and path.name in CURRENT_BUILD
 if path.suffix.lower() in {'.o','.pyc','.tmp','.a','.so','.gba','.elf','.rgba','.s16le'} or path.name=='core':return False
 if rel.parts[:2]==('generated','stage65-validation') and (path.name.startswith('failure') or path.name=='last-run.log'):
  report=path.parent/'report.json'
  if report.is_file():
   d=json.loads(report.read_text())
   if d.get('result',d.get('status'))=='PASS':return False
 with path.open('rb') as f:
  if f.read(4)==b'\x7fELF':return False
 return True

def verify_release():
 d=json.loads(REPORT.read_text());assert d['result']=='PASS' and d['stage']==70
 required=set(REQUIRED)
 for key in ('build_inputs_sha256','evidence_inputs_sha256','deliverable_sha256'):
  for name,digest in d[key].items():
   assert sha(ROOT/name)==digest,f'Changed after release gate: {name}';required.add(name)
 for key in VALIDATION_KEYS:
  for name,digest in d[key].items():
   file='generated/stage65-validation/'+name;assert sha(ROOT/file)==digest,f'Changed evidence: {name}';required.add(file)
 for name,key in [('diablo-gba-assets.gba','rom_sha256'),('diablo-gba-assets.elf','elf_sha256'),('diablo-gba-assets-build-report.json','build_report_sha256')]:
  assert sha(ROOT/'build'/name)==d[key],name
 for name in required:assert (ROOT/name).is_file(),f'Missing required deliverable: {name}'
 return d,required

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
 p.add_argument('--dry-run',action='store_true');p.add_argument('--rom-output',type=Path,help='Optional separate final ROM delivery path')
 a=p.parse_args();output=a.output.resolve();assert not output.is_relative_to(ROOT),'Write delivery archive outside project'
 if a.rom_output:
  a.rom_output=a.rom_output.resolve();assert not a.rom_output.is_relative_to(ROOT) and a.rom_output!=output
 release,required=verify_release();files=sorted(f for f in ROOT.rglob('*') if f.is_file() and include(f))
 names={str(f.relative_to(ROOT)) for f in files};assert required<=names,sorted(required-names)
 summary={'result':'PASS','file':str(output),'files':len(files)+1,'uncompressed_bytes':sum(f.stat().st_size for f in files),'rom_sha256':release['rom_sha256'],'dry_run':a.dry_run}
 if a.dry_run:print(json.dumps(summary,indent=2));return
 output.parent.mkdir(parents=True,exist_ok=True);pending=output.with_suffix('.pending.zip')
 manifest={'stage':70,'rom_sha256':release['rom_sha256'],'release_report_sha256':sha(REPORT),
  'policy':'Current source, all original assets, generated inputs, tools/tests/docs/evidence and current ROM/ELF/build report; disposable build products, historical ROM/ELF, host executables and caches omitted.',
  'files':{str(f.relative_to(ROOT)):{'bytes':f.stat().st_size,'sha256':sha(f)} for f in files}}
 with zipfile.ZipFile(pending,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
  for file in files:z.write(file,'dungeon-gba/'+str(file.relative_to(ROOT)))
  z.writestr('dungeon-gba/PACKAGE-MANIFEST-STAGE70.json',json.dumps(manifest,indent=2)+'\n')
 with zipfile.ZipFile(pending) as z:
  assert z.testzip() is None and len(z.infolist())==len(files)+1
  for name in required:assert z.read('dungeon-gba/'+name)==(ROOT/name).read_bytes(),name
  for name,record in manifest['files'].items():assert sha(ROOT/name)==record['sha256'],f'Changed during packaging: {name}'
 assert sha(REPORT)==manifest['release_report_sha256'],'Release report changed during packaging'
 pending.replace(output);summary.update(bytes=output.stat().st_size,sha256=sha(output))
 if a.rom_output:
  a.rom_output.parent.mkdir(parents=True,exist_ok=True);rom_pending=a.rom_output.with_suffix('.pending.gba')
  shutil.copyfile(ROOT/'build/diablo-gba-assets.gba',rom_pending);assert sha(rom_pending)==release['rom_sha256'];rom_pending.replace(a.rom_output)
  summary.update(rom_file=str(a.rom_output),rom_bytes=a.rom_output.stat().st_size)
 print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
