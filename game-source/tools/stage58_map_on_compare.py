#!/usr/bin/env python3
"""Summarize bounded map-on release interactions from existing native traces."""
import argparse
import json
from pathlib import Path

HZ = 16777216 / 280896
parser = argparse.ArgumentParser(description=__doc__)
for name in ('baseline-deaths', 'candidate-deaths', 'baseline-firebolt', 'candidate-firebolt', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()


def load(path):
    data = json.loads(path.read_text())
    assert data['result'] == 'PASS', path
    assert all(c['minimapOn'] for c in data['cases']), path
    return data


def tick_cadence(trace, key):
    last_tick, last_frame = trace[0][key], 0
    maximum_gap = maximum_burst = 0
    for index, sample in enumerate(trace[1:], 1):
        delta = (sample[key] - last_tick) & 65535
        if delta:
            maximum_gap = max(maximum_gap, index - last_frame)
            maximum_burst = max(maximum_burst, delta)
            last_tick, last_frame = sample[key], index
    return {'maxObservedLogicUpdateGapHardwareFrames': maximum_gap,
            'maxObservedLogicUpdateGapMs': maximum_gap * 1000 / HZ,
            'maxObservedLogicTickBurst': maximum_burst}


def death(c):
    return {'activeAnimation': c['activeAnimation'], 'fullSample': c['performance'],
            'simulationTicks': c['simulationTicks'], 'simulationTicksPerSecond': c['simulationTicksPerSecond'],
            'mapCommits': c['mapCommits'], 'mapOverruns': c['mapOverruns'],
            'logicCadence': tick_cadence(c['trace'], 'tick')}


def fire(c, report_path):
    trace = json.loads((report_path.parent / (c['name'] + '-trace.json')).read_text())
    return {**{key: c[key] for key in ('fps', 'minimumRolling60HardwareFramesFps', 'maxDrawGapMs',
                                      'simulationTicks', 'simulationTicksPerSecond', 'casts',
                                      'mapCommits', 'mapOverruns')},
            'logicCadence': tick_cadence(trace, 'ticks')}


bd, cd = load(args.baseline_deaths), load(args.candidate_deaths)
bf, cf = load(args.baseline_firebolt), load(args.candidate_firebolt)
assert bd['romSha256'] == bf['romSha256']
assert cd['romSha256'] == cf['romSha256']
assert len(bd['cases']) == len(cd['cases']) and len(bf['cases']) == len(cf['cases'])
result = {'result': 'PASS', 'baselineRomSha256': bd['romSha256'],
          'candidateRomSha256': cd['romSha256'], 'physicalGba': False,
          'limits': ['Stationary minimap enabled by actual menu input before each fixture snapshot.',
                     'Crowded deaths are seeded corpse/live inputs; casting uses real B inputs.',
                     'Active-death FPS excludes settled recovery; full-sample minimum covers 60 hardware frames.',
                     'Logic cadence is observed at hardware-frame boundaries and excludes the pre-sample interval.',
                     'Walking, discovery, cast sprite cadence and pixel correctness have separate validation.'],
          'deaths': [], 'firebolt': []}
for before, after in zip(bd['cases'], cd['cases']):
    identity = {k: before[k] for k in ('count', 'live', 'phase', 'paused', 'uniform')}
    assert all(after[k] == v for k, v in identity.items())
    assert before['performance']['hardwareFrames'] == after['performance']['hardwareFrames']
    result['deaths'].append({**identity, 'before': death(before), 'after': death(after)})
for before, after in zip(bf['cases'], cf['cases']):
    assert before['name'] == after['name'] and before['hardwareFrames'] == after['hardwareFrames']
    result['firebolt'].append({'name': before['name'], 'before': fire(before, args.baseline_firebolt),
                               'after': fire(after, args.candidate_firebolt)})
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
