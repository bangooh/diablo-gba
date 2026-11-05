from pathlib import Path
import subprocess,json,hashlib,shutil,zipfile
r=Path(__file__).resolve().parents[1];v=r/'generated/stage75-validation';o=r.parent.parent/'stage75-delivery';o.mkdir(exist_ok=True)
def arrays(base):
 b=Path(str(base)+'.gba').read_bytes();s={}
 for line in subprocess.check_output(['readelf','-sW',str(base)+'.elf'],text=True).splitlines():
  a=line.split()
  if len(a)>=8 and a[7] in ['spell_small_tiles','spell_large_tiles','spell_effect_geometry','spell_composite']:
   at=int(a[1],16)-0x8000000;s[a[7]]=hashlib.sha256(b[at:at+int(a[2],0)]).hexdigest()
 return s
old=arrays(r.parent.parent/'stage73/dungeon-gba/generated/stage73-validation/release-rom/diablo-gba-assets');new=arrays(v/'release-rom/diablo-gba-assets');assert len(old)==4 and old==new
comparison={'result':'PASS','stage73':old,'stage75':new};(v/'guardian-rom-comparison.json').write_text(json.dumps(comparison,indent=2)+'\n')
with zipfile.ZipFile(r.parent.parent/'stage73-delivery/dungeon-gba-post086-fixes-source.zip') as z:original=z.read('dungeon-gba/generated/spell_expansion_assets.h')
assert (r/'generated/spell_expansion_assets.h').read_bytes()==original
b=json.loads((v/'release-rom/diablo-gba-assets-build-report.json').read_text());n=json.loads((v/'native/report.json').read_text());assert n['result']=='PASS' and n['romSha256']==b['sha256']
assert '"checks":41344' in (r/'generated/stage75-books-test.log').read_text()
report={'build':b,'native':n,'guardianStage73':comparison,'environmentChecks':41344,'saveVersion':130,'generatorVersion':20,'linkVersion':140};(r/'generated/stage75-release-report.json').write_text(json.dumps(report,indent=2)+'\n')
for source,name in [(v/'release-rom/diablo-gba-assets.gba','diablo-gba-stage75.gba'),(v/'native/bookcase-62.png','stage75-book-name.png')]:shutil.copy2(source,o/name)
with zipfile.ZipFile(r.parent.parent/'stage74-delivery/dungeon-gba-post086-fixes-source.zip') as z:paths={Path(n).relative_to('dungeon-gba') for n in z.namelist() if not n.endswith('/')}
for name in ['README.md','STATUS-STAGE75.md','build.py','src/game.h','src/link.h','src/environment_objects.inc','src/fountain_objects.inc','src/infopanel.inc','tools/package_stage75.py','tools/stage75_book_validation.mjs','tests/environment-objects-stage61.c','tests/environment-objects-stage64.c','tests/run-stage75-books.py','generated/spell_expansion_assets.h']:paths.add(Path(name))
paths.update(p.relative_to(r) for p in (r/'generated').iterdir() if p.is_file() and 'stage75' in p.name)
paths.update(p.relative_to(r) for p in v.rglob('*') if p.is_file() and 'release-rom' not in p.parts)
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip','w',zipfile.ZIP_DEFLATED,6) as z:
 for p in sorted(paths):
  source=r/p
  if str(p) in ['build/diablo-gba-assets.gba','build/diablo-gba-assets.elf','build/diablo-gba-assets-build-report.json']:source=v/'release-rom'/p.name
  z.write(source,str(Path('dungeon-gba')/p))
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip') as z:
 assert z.testzip() is None
 assert hashlib.sha256(z.read('dungeon-gba/build/diablo-gba-assets.gba')).hexdigest()==b['sha256']
 assert z.read('dungeon-gba/generated/spell_expansion_assets.h')==original
print('PASS deliverable ROM, archived ROM and Stage73 assets',b['sha256'])
