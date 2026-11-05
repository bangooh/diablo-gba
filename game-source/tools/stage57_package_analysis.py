#!/usr/bin/env python3
"""Package analysis evidence only; never rewrite the canonical source or ROM."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('destination', type=Path)
parser.add_argument('--private-baseline', type=Path, required=True)
args = parser.parse_args()

digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
provenance = json.loads((root / 'generated/stage56-validation/source-provenance.json').read_text())
assert digest(root / 'build/diablo-gba-assets.gba') == provenance['rom_sha256']
for name, expected in provenance['source_files'].items():
    assert digest(root / name) == expected, name

analysis = root / 'generated/stage57-analysis'
for name in ('minimap/summary.json', 'deaths/summary.json', 'firebolt/summary.json'):
    assert (analysis / name).is_file(), name
for path in analysis.rglob('report.json'):
    assert json.loads(path.read_text())['result'] == 'PASS', path

files = {str(p.relative_to(root)): p for p in analysis.rglob('*') if p.is_file()}
files['STAGE57-PERFORMANCE-ANALYSIS.md'] = root / 'STAGE57-PERFORMANCE-ANALYSIS.md'
for path in (root / 'tools').glob('stage57_*'):
    if path.is_file():
        files[str(path.relative_to(root))] = path
for name in ('src/automap_raster.inc', 'src/gba_mode0.c', 'src/hardware_light.inc',
             'src/lighting.inc', 'src/death_sprites.inc', 'src/floor_dirty.inc',
             'src/arrows.c', 'src/spells.c', 'src/motion.h'):
    files['reviewed-source/' + name] = root / name
files['baseline/source-provenance.json'] = root / 'generated/stage56-validation/source-provenance.json'
files['baseline/private-build-provenance.json'] = args.private_baseline / 'private-build-provenance.json'

manifest = {
    'task': 'Stage57 analysis only: minimap, Firebolt and simultaneous deaths',
    'baselineRomSha256': provenance['rom_sha256'],
    'productionSourceFilesVerifiedUnchanged': len(provenance['source_files']),
    'productionRomUnchanged': True,
    'physicalHardwareMeasured': False,
    'reproduction': 'Overlay tools/ on the Stage56 source project; see STAGE57-PERFORMANCE-ANALYSIS.md. This archive is not a full source tree or release ROM.',
    'independentAudit': [
        'Minimap and Firebolt average FPS and rolling60 minima recomputed from raw traces.',
        'Minimap OFF/ON logical coordinate sequences matched in both fog fixtures.',
        'Render counter counts completed draw iterations, not unique visible pixel frames.',
        'VBlank overrun telemetry confirms boundary crossing, not per-frame visible tearing.',
        'Projectile logic cadence differs from irregular OAM presentation cadence.',
        'Death active-window averages and full180HW rolling minima are reported separately.',
    ],
    'files': {name: {'sha256': digest(path), 'bytes': path.stat().st_size}
              for name, path in sorted(files.items())},
}
args.destination.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name, path in sorted(files.items()):
        z.write(path, name)
    z.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
with zipfile.ZipFile(args.destination) as z:
    assert z.testzip() is None
print(json.dumps({'path': str(args.destination.resolve()), 'bytes': args.destination.stat().st_size,
                  'sha256': digest(args.destination), 'files': len(files) + 1,
                  'productionUnchanged': True}))
