#!/usr/bin/env python3
"""Compare actual Stage63/Stage64 door input fixtures and native outcomes."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALIDATION = ROOT / 'generated/stage64-validation'


def main():
    old = json.loads((VALIDATION / 'door-controls-baseline/report.json').read_text())
    new = json.loads((VALIDATION / 'door-controls-native/report.json').read_text())
    assert old['result'] == new['result'] == 'PASS'
    a = {c['name']: c for c in old['cases']}
    b = {c['name']: c for c in new['cases']}
    assert a.keys() == b.keys()
    checks = []
    for name, c in b.items():
        previous = a[name]
        if c['kind'] in ('generated-door', 'generated-diagonal-wall-door'):
            assert c['fixture'] == previous['fixture'], name + ': identical input and doorway fixture'
            assert c['terrainSha256'] == previous['terrainSha256'], name + ': identical native terrain'
            assert c['enteredDoor'] and c['continuedThrough']
            assert not previous['enteredDoor'] and not previous['continuedThrough']
            checks.append({'name': name, 'result': 'PASS', 'sameGeneratedFixture': True,
                           'sameNativeTerrain': True, 'baselineEnteredDoor': False,
                           'candidateEnteredDoor': True, 'candidateContinuedThrough': True,
                           'key': c['fixture']['key']})
        elif c['kind'] == 'generated-wall-end':
            assert len(previous['positions']) == 1 and len(c['positions']) == 2
            assert c['stableTailHardwareFrames'] == 120 and c['manualReverse']
            checks.append({'name': name, 'result': 'PASS',
                           'sameGeneratedFixture': c['fixture'] == previous['fixture'],
                           'sameNativeTerrain': c['terrainSha256'] == previous['terrainSha256'],
                           'baselineStationary': True, 'candidateSlidesThenStopsWithoutPingpong': True,
                           'candidateManualReverse': True,
                           'note': 'The selected wall-end coordinates differ because live prop placement changed. These are independent correctness cases, not a same-position comparison.'})
    assert a['closed-door-chest-priority']['actualAOperated'] == 'chest'
    assert b['closed-door-chest-priority']['actualAOperated'] == 'closed-door'
    checks.append({'name': 'closed-door-priority', 'result': 'PASS',
                   'baselineAction': 'chest', 'candidateAction': 'closed-door'})
    assert a['occupied-open-door']['actorCollisionPreserved'] and b['occupied-open-door']['actorCollisionPreserved']
    checks.append({'name': 'actor-collision', 'result': 'PASS', 'baselineAndCandidateRejectOccupiedDoor': True})
    assert len(checks) == 11
    report = {
        'result': 'PASS', 'romSha256': new['romSha256'], 'baselineRomSha256': old['romSha256'],
        'checks': checks,
        'method': 'Same runnable native input harness. Eight doorway cases assert identical generated terrain hashes and full fixture coordinates/door identities before comparing physical input results.',
        'harnessSha256': hashlib.sha256((ROOT / 'tools/stage64_door_controls_native.mjs').read_bytes()).hexdigest(),
        'toolSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'limits': ['Targeted generated L1/L2 geometry, not every campaign seed.',
                   'Stage64 themed props differ elsewhere. Wall-end coordinates differ and no aggregate performance comparison is asserted.'],
        'reproduce': 'python3 tools/stage64_door_controls_compare.py',
    }
    out = VALIDATION / 'door-controls-comparison'
    out.mkdir(exist_ok=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 8 identical generated doorway/input comparisons; priority, occupancy, and independent wall-end checks.')


if __name__ == '__main__':
    main()
