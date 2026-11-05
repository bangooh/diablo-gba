from pathlib import Path
import json,hashlib,shutil,zipfile
r=Path(__file__).resolve().parents[1];v=r/'generated/stage74-validation';o=r.parent.parent/'stage74-delivery';o.mkdir(exist_ok=True)
b=json.loads((v/'release-rom/diablo-gba-assets-build-report.json').read_text());n=json.loads((v/'native/report.json').read_text());assert n['result']=='PASS' and n['romSha256']==b['sha256']
report={'build':b,'native':n,'assetComparison':{'smallGuardianFramesChanged':127,'largeGuardianFramesChanged':31,'transparency':'identical','geometry':'identical','sharedPalette':'identical','otherEffects':'byte-identical'},'saveVersion':130,'linkVersion':139};(r/'generated/stage74-release-report.json').write_text(json.dumps(report,indent=2)+'\n')
for source,name in [(v/'release-rom/diablo-gba-assets.gba','diablo-gba-stage74.gba'),(v/'guardian-comparison.png','stage74-guardian-comparison.png'),(v/'native/13-Guardian.gif','stage74-guardian.gif')]:shutil.copy2(source,o/name)
with zipfile.ZipFile(r.parent.parent/'stage73-delivery/dungeon-gba-post086-fixes-source.zip') as z:paths={Path(n).relative_to('dungeon-gba') for n in z.namelist() if not n.endswith('/')}
for name in ['README.md','GUARDIAN-COLOR-STAGE74.md','build.py','tools/package_stage74.py','tools/pack_spell_geometry_stage74.py','tools/stage74_guardian_preview.py','generated/spell_expansion_assets.h']:paths.add(Path(name))
paths.update(p.relative_to(r) for p in (r/'generated').iterdir() if p.is_file() and 'stage74' in p.name)
paths.update(p.relative_to(r) for p in (v/'native').iterdir() if p.is_file() and p.suffix in ('.gif','.json','.c'))
paths.add((v/'guardian-comparison.png').relative_to(r))
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip','w',zipfile.ZIP_DEFLATED,6) as z:
 for p in sorted(paths):
  source=r/p
  if str(p) in ['build/diablo-gba-assets.gba','build/diablo-gba-assets.elf','build/diablo-gba-assets-build-report.json']:source=v/'release-rom'/p.name
  z.write(source,str(Path('dungeon-gba')/p))
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip') as z:
 assert z.testzip() is None
 assert hashlib.sha256(z.read('dungeon-gba/build/diablo-gba-assets.gba')).hexdigest()==b['sha256']
print('PASS deliverable ROM and archived ROM match',b['sha256'])
