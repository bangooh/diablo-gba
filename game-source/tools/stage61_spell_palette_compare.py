"""Paired native snapshots: four new colors, all existing users unchanged."""
from pathlib import Path
import hashlib
import json
import sys

before,after=map(Path,sys.argv[1:3])
old=json.loads((before/'report.json').read_text())
new=json.loads((after/'report.json').read_text())
assert old['gammaIndex']==new['gammaIndex']==9
report={'result':'PASS','baselineRomSha256':old['romSha256'],'candidateRomSha256':new['romSha256'],'gammaUiLevel':10,'checks':{}}
for name in ['baseObj','hardwareObj','deathLut','arrowTiles','stairsTiles','beltTiles']:
    a=(before/(name+'.bin')).read_bytes();b=(after/(name+'.bin')).read_bytes()
    if name in ['baseObj','hardwareObj']:
        changed=[i//2 for i in range(0,512,2) if a[i:i+2]!=b[i:i+2]]
        assert changed==[59,60,61,62],(name,changed)
        report['checks'][name]={'changed_entries':changed,'other_entries_unchanged':252}
    else:
        assert a==b,name
        report['checks'][name]={'unchanged_bytes':len(a),'sha256':hashlib.sha256(a).hexdigest()}
    if name=='deathLut':
        assert not any(b.count(i) for i in range(59,63))
print(json.dumps(report,indent=2))
