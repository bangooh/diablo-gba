"""Compare captured final-ROM PCM with gain-scaled native sources."""
from pathlib import Path
import argparse
import hashlib
import json
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('runtime', type=Path)
parser.add_argument('rom', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
runtime = json.loads((args.runtime / 'report.json').read_text())
music_raw = np.frombuffer((root / 'generated/audio_music_town.lz').read_bytes(), dtype=np.int8).astype(int)
effect_raw = np.frombuffer((root / 'generated/audio_monster_129.lz').read_bytes(), dtype=np.int8).astype(int)


def scaled(raw, numerator, denominator):
    magnitude = (abs(raw) * numerator + denominator // 2) // denominator
    return np.clip(np.where(raw < 0, -magnitude, magnitude), -128, 127)


music = scaled(music_raw, 780, 1000)
report = {'romSha256': hashlib.sha256(args.rom.read_bytes()).hexdigest(),
          'note': 'Native-sample lag is fitted to account for emulator output resampling. '
                  'Music correlation and effect RMS are supporting waveform evidence, '
                  'not proof of no FIFO underruns. Host fixtures verify exact ordering.',
          'mixed': []}
cases = [('music', 'bgm-runtime.s16le', runtime, 60000)]
cases += [('mixed', f'bgm-mixed-{meta["soundLevel"]}.s16le', meta, 0)
          for meta in runtime['mixed']]
for name, filename, meta, limit in cases:
    heard = np.fromfile(args.runtime / filename, dtype='<i2').reshape(-1, 2)[:, 0] / 192
    if limit:
        heard = heard[:limit]
    rate = (meta['endSample'] - meta['startSample']) / (meta['pcmBytes'] / 4)
    effects = scaled(effect_raw * 2, meta['soundLevel'], 5) if name == 'mixed' else None
    best = None
    for lag in np.arange(-32, 33, .125):
        positions = np.floor(meta['startSample'] + np.arange(len(heard)) * rate + lag).astype(int)
        want = music[positions] if name == 'music' else music[positions-1] + effects[positions-meta['voiceStart']]
        error = float(np.mean((heard-want)**2))
        if best is None or error < best['meanSquareError']:
            best = {'meanSquareError': error, 'fittedNativeSampleLag': float(lag),
                    'correlation': float(np.corrcoef(heard, want)[0, 1]),
                    'observedRms': float(np.std(heard)), 'expectedRms': float(np.std(want)),
                    'captureSamplesAnalyzed': len(heard)}
    if name == 'music':
        report['music'] = best
        assert best['correlation'] > .99, best
    else:
        best['soundLevel'] = meta['soundLevel']
        best['rmsRatio'] = best['observedRms'] / best['expectedRms']
        assert .9 < best['rmsRatio'] < 1.1, best
        report['mixed'].append(best)
assert all(a['observedRms'] < b['observedRms'] for a, b in zip(report['mixed'], report['mixed'][1:]))
report['result'] = 'PASS'
(args.runtime / 'waveform.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
