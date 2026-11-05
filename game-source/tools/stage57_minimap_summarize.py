"""Validate paired current-ROM observations and emit a compact summary."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
report = json.loads((root / 'report.json').read_text())
assert report['result'] == 'PASS'
groups = {}
for sample in report['samples']:
    key = (sample['region'], sample['mode'], sample['mapOn'])
    groups.setdefault(key, []).append(sample)
stable_fields = ['renders', 'fps', 'minRolling60HardwareFramesFps',
                 'maxRenderGapHardwareFrames', 'renderGapHistogram',
                 'logicTicks', 'logicalMoves', 'cameraChanges', 'mapChanges',
                 'commitLatenciesHardwareFrames', 'mapOverruns', 'outlineBuilds',
                 'pcSamples']
pairs = []
for region, mode in dict.fromkeys((k[0], k[1]) for k in groups):
    values = []
    for on in [False, True]:
        samples = groups[(region, mode, on)]
        assert len(samples) >= 2, 'two repeat observations required'
        assert all(all(s[f] == samples[0][f] for f in stable_fields) for s in samples)
        s = samples[0]
        values.append({f: s[f] for f in stable_fields})
    off, on = values
    assert off['logicTicks'] == on['logicTicks']
    assert off['logicalMoves'] == on['logicalMoves']
    pairs.append({'region': region, 'mode': mode, 'repeatCount': len(samples),
                  'repeatsIdentical': True, 'off': off, 'on': on,
                  'averageFpsReductionPercent': (off['fps']-on['fps'])/off['fps']*100})
summary = {'result': 'PASS', 'romSha256': report['romSha256'],
           'hardwareHz': report['hardwareHz'],
           'rollingWindowHardwareFrames': 60,
           'rollingWindowMilliseconds': 60000/report['hardwareHz'],
           'pairs': pairs, 'staticVisualChecks': {k: {f: v[f] for f in
               ['samePalette', 'sameTerrainPixels', 'sameHardwareLightMask']}
               for k, v in report['states'].items()}}
(root / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
for p in pairs:
    print(p['region'], p['mode'], 'OFF', round(p['off']['fps'], 2),
          'ON', round(p['on']['fps'], 2),
          'drop', round(p['averageFpsReductionPercent'], 2), '%')
