"""Summarize accepted minimap traces without rerunning the emulator."""
from pathlib import Path
import collections
import json
import statistics
import sys

root = Path(__file__).resolve().parents[1]
folder = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'generated/stage45-minimap/controlled'
report = json.loads((folder / 'report.json').read_text())
summary = {'romSha256': report['romSha256'], 'result': report['result'], 'fixtureWorld': report.get('fixtureWorld'), 'fixtureValidation': report.get('fixtureValidation'), 'samples': [], 'stationaryVisualChecks': report['states']}
for sample in report['samples']:
    trace = json.loads((folder / (sample['name'] + '-trace.json')).read_text())
    prev = sample['before']
    moves, commits, camera = [], [], []
    for state in trace:
        if (state['x'], state['y']) != (prev['x'], prev['y']):
            moves.append((state['f'], state['x'], state['y']))
        if state['mapCommits'] != prev['mapCommits']:
            commits.append((state['f'], state['mapLastX'], state['mapLastY']))
        if (state['bgX'], state['bgY']) != (prev['bgX'], prev['bgY']):
            camera.append((state['f'], state['bgX'], state['bgY']))
        prev = state
    latencies = [next((c[0] - m[0] for c in commits if c[0] >= m[0] and c[1:] == m[1:]), None) for m in moves]
    latencies = [n for n in latencies if n is not None]
    row = {key: sample[key] for key in ['name', 'region', 'mapOn', 'walking', 'rep', 'fps', 'minRollingSecondFps', 'maxRenderGapMs', 'logicalMoves', 'logicTicks', 'mapOverruns', 'outlineBuilds']}
    row.update(cameraUpdates=len(camera), mapCommits=len(commits), moveToMapCommitFrames=dict(collections.Counter(latencies)))
    if latencies:
        row['moveToMapCommitMs'] = {'min': min(latencies)*1000/report['hardwareHz'], 'max': max(latencies)*1000/report['hardwareHz'], 'mean': statistics.mean(latencies)*1000/report['hardwareHz']}
    summary['samples'].append(row)

(folder / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({'result': summary['result'], 'sampleCount': len(summary['samples']), 'file': str(folder/'summary.json')}))
