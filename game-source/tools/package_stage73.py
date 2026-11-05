from pathlib import Path
import json,hashlib,shutil,zipfile
r=Path(__file__).resolve().parents[1];v=r/'generated/stage73-validation';o=r.parent.parent/'stage73-delivery';o.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
build=json.loads((v/'release-rom/diablo-gba-assets-build-report.json').read_text());native=json.loads((v/'final-native/report.json').read_text());spells=json.loads((v/'final-spells/report.json').read_text())
assert native['result']==spells['result']=='PASS';assert native['romSha256']==spells['romSha256']==build['sha256'];assert sha(v/'release-rom/diablo-gba-assets.gba')==build['sha256']
# Legacy Korean input below means 'IWRAM 814 bytes'; normalize the updated value to English.
p=r/'APOCALYPSE-GUARDIAN-STAGE73.md';s=p.read_text().replace('IWRAM 814바이트','IWRAM 916 bytes').replace('IWRAM 814 bytes','IWRAM 916 bytes')
s+='\n## Final measurements with minimap ON\n\n|Scene|Average FPS|Lowest 1-second FPS|\n|---|---:|---:|\n'
for row in native['performance']:s+=f"|{ {24:'Repeated Apocalypse casts',13:'Guardian'}.get(row.get('spell'),'Movement')}|{row['averageFps']:.1f}|{row['lowestOneSecondFps']:.1f}|\n"
s+='\nEmulator measurements use a fixed L1 room with 8 monsters and 720 hardware frames per case. They do not guarantee a minimum of 24 FPS in all combat. At the user\'s request, the final version displays four explosions at a time. Presentation takes half as long as with two at a time, accepting FPS drops during casting.\n';p.write_text(s)
report={'build':build,'native':native,'spellDemos':spells,'host':'PASS ASan/UBSan all112, full6400cell queue, slot saturation, repeated casts, reset','atlasCases':1684,'guardian':'PASS all original emergence15/idle14/attack3 frames','saveVersion':130,'linkVersion':139,'limits':['Original positions, coalesced waiting cells on repeated casts','4 concurrent explosions; long presentation with many targets','No physical GBA/link verification']}
(r/'generated/stage73-release-report.json').write_text(json.dumps(report,indent=2)+'\n')
shutil.copy2(v/'release-rom/diablo-gba-assets.gba',o/'diablo-gba-stage73.gba');shutil.copy2(v/'guardian-comparison.png',o/'stage73-guardian-comparison.png');shutil.copy2(v/'final-native/guardian-isolated.gif',o/'stage73-guardian.gif');shutil.copy2(v/'final-spells/24-Apocalypse.gif',o/'stage73-apocalypse.gif')
with zipfile.ZipFile(r.parent.parent/'stage72-delivery/dungeon-gba-post086-fixes-source.zip') as z:paths={Path(n).relative_to('dungeon-gba') for n in z.namelist() if not n.endswith('/')}
for folder in ['src','tools','tests','source_assets']:
 paths.update(p.relative_to(r) for p in (r/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.o'))
paths.update(p.relative_to(r) for p in r.iterdir() if p.is_file())
paths.update(p.relative_to(r) for p in (r/'generated').iterdir() if p.is_file() and 'stage73' in p.name and p.suffix in ('.json','.log'))
for folder in ['final-native','final-spells']:
 paths.update(p.relative_to(r) for p in (v/folder).iterdir() if p.is_file() and p.suffix in ('.json','.gif','.png','.c'))
paths.add((v/'guardian-comparison.png').relative_to(r))
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip','w',zipfile.ZIP_DEFLATED,6) as z:
 for p in sorted(paths):
  source=r/p
  if str(p) in ['build/diablo-gba-assets.gba','build/diablo-gba-assets.elf','build/diablo-gba-assets-build-report.json']:source=v/'release-rom'/p.name
  z.write(source,str(Path('dungeon-gba')/p))
with zipfile.ZipFile(o/'dungeon-gba-post086-fixes-source.zip') as z:
 assert z.testzip() is None;assert hashlib.sha256(z.read('dungeon-gba/build/diablo-gba-assets.gba')).hexdigest()==build['sha256']
manifest={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in o.iterdir() if p.is_file()};(o/'delivery-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
