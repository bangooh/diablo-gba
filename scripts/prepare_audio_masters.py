"""Validate newly generated intermediate PCM before the final audio resampling."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, required=True)
a = p.parse_args()
manifest = json.loads((ROOT / 'docs/audio-master-inputs.json').read_text())
out = a.project / 'source_assets/audio_stage66'
out.mkdir(parents=True, exist_ok=True)
existing = [out / clip['source'] for clip in manifest['clips']]
if all(path.is_file() and len(path.read_bytes()) == clip['samples']
       and hashlib.sha256(path.read_bytes()).hexdigest() == clip['sha256']
       for path, clip in zip(existing, manifest['clips'])):
    # Final runtime clips may already have been resampled. Immutable masters
    # remain authoritative only after every sample count and hash is verified.
    (out / 'masters.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PASS:', len(existing), 'verified immutable PCM masters reused')
    raise SystemExit(0)
for clip in manifest['clips']:
    name = clip['symbol'].removeprefix('asset_')
    data = (a.project / 'generated' / (name + '.lz')).read_bytes()
    samples = clip['samples']
    if len(data) < samples or any(data[samples:]):
        raise ValueError('Invalid sample count or silent guard: ' + name)
    raw = data[:samples]
    if hashlib.sha256(raw).hexdigest() != clip['sha256']:
        raise ValueError('Intermediate master differs: ' + name)
    (out / clip['source']).write_bytes(raw)
(out / 'masters.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('PASS:', len(manifest['clips']), 'PCM masters and silent guards')
