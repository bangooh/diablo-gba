"""Preserve original monster clips before palette sharing rewrites runtime files."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
source = a.project / 'generated'
a.output.mkdir(parents=True, exist_ok=True)
manifest = a.output / 'manifest.json'
if manifest.exists():
    hashes = json.loads(manifest.read_text())
else:
    records = json.loads((a.project / 'source_assets/monster_catalog/monsters.json').read_text())['monsters']
    names = {'hero_frame_pool.lz'}
    for row in records:
        key = row['key']
        names.add(('obj' if key == 'zombie' else key) + '_palette.lz')
        for state in 'nwahs':
            name = key + '_' + state + '.lz'
            if not (source / name).exists() and state == 'h':
                name = key + '_hit.lz'
            if (source / name).exists():
                names.add(name)
    hashes = {}
    for name in sorted(names):
        data = (source / name).read_bytes()
        (a.output / name).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    manifest.write_text(json.dumps(hashes, indent=2) + '\n')
for name, expected in hashes.items():
    assert hashlib.sha256((a.output / name).read_bytes()).hexdigest() == expected, name
print('PASS', len(hashes), 'immutable monster inputs')
