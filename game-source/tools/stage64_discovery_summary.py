#!/usr/bin/env python3
"""Summarize Stage64 pacing and account for every recorded discovery.

World scroll, completed video and CPU-camera observations are distinct. PC
samples identify associations only; optional probe timers measure elapsed CPU
work including interrupts. Never infer warmup counters from the first trace row.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('folder', type=Path)
ROOT = Path(__file__).resolve().parents[1]
parser.add_argument('--rom', type=Path, default=ROOT/'build/diablo-gba-assets.gba')
args = parser.parse_args()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def analyze(folder):
    report = json.loads((folder/'report.json').read_text())
    assert report['result'] == 'PASS'
    assert report['romSha256'] == sha(args.rom), 'Summary must describe the selected final ROM'
    hz = report['hardwareHz']
    period = 1000/hz
    output = {'result':'PASS', 'romSha256':report['romSha256'], 'hardwareHz':hz,
              'sourceSha256':{str(path.resolve().relative_to(ROOT)):sha(path) for path in [Path(__file__),folder/'report.json']},
              'fixtureWorld':report['fixtureWorld'], 'samples':[],
              'notes':['Every condition uses its own persisted beginning state after warmup.',
                       'Intervals start from frame0; the premeasurement portion of its first held frame is outside the sample.',
                       'Video hashes measure actual identical completed frames; animation/map changes can hide world motion stalls.',
                       'BG0 scroll registers independently measure world translation; CPU camera state is not a displayed frame.',
                       'Discovery windows end before the next reveal: a later unrelated commit is never attached to an earlier no-op reveal.',
                       'A reveal without a commit is recorded as an observation; the separate pixel oracle must establish whether its displayed contours were unchanged.',
                       'PC samples are once per hardware frame, not exclusive CPU cycle profiling.',
                       'Probe timers include interrupts and small instrumentation overhead; release ROM pacing is authoritative.',
                       'One peaceful L1 seed12345 corridor per condition; not all floors, seeds, combat or physical GBA.']}

    for sample in report['samples']:
        beginning = report['beginningStates'][sample['name']]
        assert beginning == sample['before'] and beginning['f'] == 0
        assert {'revision','videoSha256','playfieldVideoSha256','diag','camX','bgX'} <= beginning.keys()
        trace_path = folder/(sample['name']+'-trace.json')
        trace = json.loads(trace_path.read_text())
        frames = sample['hardwareFrames']
        assert frames >= 600 and [x['f'] for x in trace] == list(range(1,frames+1)), 'Missing or duplicated hardware observation'
        assert all(trace[-1][key] == value for key,value in sample['after'].items()), 'Trace/report endpoint differs'
        output['sourceSha256'][str(trace_path.resolve().relative_to(ROOT))] = sha(trace_path)
        pairs = list(zip([beginning]+trace[:-1],trace))
        revision_changes = [(a,b) for a,b in pairs if b['revision'] != a['revision']]
        cells_changed = sum(b['revealedCells']-a['revealedCells'] for a,b in pairs)
        revisions_changed = sum(b['revision']-a['revision'] for a,b in pairs)
        assert all(b['revision']-a['revision'] == b['revealedCells']-a['revealedCells'] >= 0 for a,b in pairs), 'Reveal/revision counters differ or regress'
        assert cells_changed == trace[-1]['revealedCells']-beginning['revealedCells'] == revisions_changed == trace[-1]['revision']-beginning['revision']
        assert sum((a['x'],a['y']) != (b['x'],b['y']) for a,b in pairs) == sample['logicalMoves']
        assert all(b['renders']-a['renders'] in (0,1) for a,b in pairs)
        assert trace[-1]['renders']-beginning['renders'] == sample['renders']
        assert math.isclose(sample['fps'],sample['renders']*hz/frames,rel_tol=1e-12)

        def intervals(fields):
            key = lambda row: tuple(row[name] for name in fields)
            previous = key(beginning)
            last = 0
            result = []
            for row in trace:
                value = key(row)
                if value != previous:
                    result.append({'startFrame':last, 'endFrame':row['f'], 'hardwareFrames':row['f']-last})
                    last = row['f']
                    previous = value
            histogram = dict(sorted(collections.Counter(x['hardwareFrames'] for x in result).items()))
            ordered = sorted(x['hardwareFrames'] for x in result)
            percentile = lambda q: ordered[max(0, math.ceil(q*len(ordered))-1)] if ordered else None
            return {'intervals':result,'histogramHardwareFrames':histogram,'intervalCount':len(result),
                    'p99HardwareFrames':percentile(.99), 'p99Ms':percentile(.99)*period if ordered else None,
                    'maxHardwareFrames':max(ordered) if ordered else None,
                    'maxMs':max(ordered)*period if ordered else None,
                    'trailingUnfinishedHoldHardwareFrames':frames-last}

        metrics = {name:intervals(fields) for name,fields in [
            ('render',('renders',)),('video',('videoSha256',)),
            ('playfieldVideo',('playfieldVideoSha256',)),('worldBg0Scroll',('bgX','bgY')),
            ('cpuCamera',('camX','camY'))]}
        assert {str(k):v for k,v in metrics['render']['histogramHardwareFrames'].items()} == sample['renderGapHistogram']
        assert metrics['render']['intervalCount'] == sample['renders']
        previous = beginning
        fog_transitions, fog_groups, commits, anchor_changes = [], [], [], []
        for row in trace:
            if (row['mapLastX'],row['mapLastY']) != (previous['mapLastX'],previous['mapLastY']):
                anchor_changes.append({'frame':row['f'],
                    'before':[previous['mapLastX'],previous['mapLastY']],
                    'after':[row['mapLastX'],row['mapLastY']]})
            if row['revision'] != previous['revision']:
                transition = {'frame':row['f'],'oldRevision':previous['revision'],'newRevision':row['revision'],
                              'newCells':row['revealedCells']-previous['revealedCells'],'hero':[row['x'],row['y']]}
                fog_transitions.append(transition)
                # A reveal can straddle a hardware-frame boundary. Group only
                # adjacent observations of one logical cell, keeping raw data.
                if fog_groups and fog_groups[-1]['hero']==transition['hero'] and row['f']<=fog_groups[-1]['lastFrame']+1:
                    fog_groups[-1]['lastFrame']=row['f']
                    fog_groups[-1]['newRevision']=transition['newRevision']
                    fog_groups[-1]['newCells']+=transition['newCells']
                    fog_groups[-1]['rawTransitionFrames'].append(row['f'])
                else:
                    fog_groups.append({**transition,'lastFrame':row['f'],'rawTransitionFrames':[row['f']]})
            if row['mapCommits'] != previous['mapCommits']:
                assert row['mapCommits']-previous['mapCommits']==1
                commits.append({'frame':row['f'],'revision':row['revision'],'mapTiles':row['mapTiles'],
                                'anchor':[row['mapLastX'],row['mapLastY']],
                                'bg0Scroll':[row['bgX'],row['bgY']],
                                'cpuCameraMod512':[row['camX']&511,row['camY']&511]})
            previous = row

        events = []
        for index,fog in enumerate(fog_groups):
            window_end = fog_groups[index+1]['frame'] if index+1<len(fog_groups) else len(trace)+1
            window_commits = [c for c in commits if fog['frame']<=c['frame']<window_end]
            following = next((c for c in window_commits if c['frame']>=fog['lastFrame'] and c['revision']>=fog['newRevision']),None)
            before_reveal = beginning if fog['frame']==1 else trace[fog['frame']-2]
            at_reveal = trace[fog['frame']-1]
            after_window = trace[window_end-2]
            window_anchors = [c for c in anchor_changes if fog['frame']<=c['frame']<window_end]
            event = {**fog,'commit':following,'windowEndFrameExclusive':window_end,
                     'commitsUntilNextReveal':window_commits,'noCommitObserved':not window_commits,
                     'anchorBeforeReveal':[before_reveal['mapLastX'],before_reveal['mapLastY']],
                     'anchorAtReveal':[at_reveal['mapLastX'],at_reveal['mapLastY']],
                     'anchorAfterWindow':[after_window['mapLastX'],after_window['mapLastY']],
                     'anchorChanges':window_anchors,'anchorStable':not window_anchors,
                     'anchorChanged':bool(window_anchors),
                     'uniqueTilesBeforeReveal':before_reveal['mapTiles'],
                     'uniqueTilesAtReveal':at_reveal['mapTiles'],
                     'uniqueTilesAfterWindow':after_window['mapTiles']}
            for name in ['render','video','playfieldVideo','worldBg0Scroll','cpuCamera']:
                # First completed change strictly after the reveal begins.
                gap = next((x for x in metrics[name]['intervals'] if x['startFrame']<=fog['frame']<x['endFrame']),None)
                event[name] = None if gap is None else {**gap,'ms':gap['hardwareFrames']*period}
            if following:
                committed_frame = trace[following['frame']-1]
                later_scroll = next((x for x in trace[following['frame']:] if (x['bgX'],x['bgY'])!=(committed_frame['bgX'],committed_frame['bgY'])),None)
                event['worldScrollResumesAfterCommitFrame'] = later_scroll['f'] if later_scroll else None
                event['cameraAheadOfWorldAtCommit'] = following['bg0Scroll'] != following['cpuCameraMod512']
            end = following['frame'] if following else fog['lastFrame']
            event['associatedPcSamples'] = dict(collections.Counter(x['fn'] for x in trace if fog['frame']<=x['f']<=end))
            events.append(event)

        diagnostic = None
        if beginning['diag'] is not None:
            assert len(beginning['diag']) == 40
            prior = {name:beginning['diag'][done] for name,begin,done in [('rebuild',0,1),('discovery',21,38),('commit',28,39)]}
            diagnostic = {'initialCounters':beginning['diag'],'rebuilds':[],'discoveries':[],'commits':[]}
            for row in trace:
                d = row['diag']
                for name,begin,done in [('rebuild',0,1),('discovery',21,38),('commit',28,39)]:
                    if d[begin]!=d[done] or d[done]<=prior[name]: continue
                    missed = d[done]-prior[name]-1
                    prior[name] = d[done]
                    if name=='rebuild':
                        diagnostic['rebuilds'].append({'frame':row['f'],'serial':d[done],'missedCompletedEvents':missed,
                            'reasonBits':d[10],'newCells':d[13],'totalMs':d[4]/16.384,'outlineMs':d[5]/16.384,
                            'rasterMs':d[6]/16.384,'packMs':d[7]/16.384,'prepareMs':d[8]/16.384,
                            'packAttempts':d[9],'fallbackBefore':d[16],'fallbackAfter':d[17],'uniqueTiles':d[18],
                            'uploadBytes':d[19],'status':d[20]})
                    elif name=='commit':
                        diagnostic['commits'].append({'frame':row['f'],'serial':d[done],'rebuildSerial':d[29],
                            'missedCompletedEvents':missed,'totalMs':d[32]/16.384,'uploadMs':d[33]/16.384,
                            'vcountBefore':d[34],'vcountAfter':d[35],'status':d[36],'uploadBytes':d[37]})
                    elif d[27]:
                        diagnostic['discoveries'].append({'frame':row['f'],'serial':d[done],'newCells':d[27],
                            'durationMs':d[24]/16.384,'missedCompletedEvents':missed})
            for key in ('rebuilds','commits'):
                assert all(not x['missedCompletedEvents'] for x in diagnostic[key]), 'Hardware sampling skipped a rebuild/commit'

        assert [x['frame'] for x in fog_transitions] == [b['f'] for a,b in revision_changes]
        assert [f for group in fog_groups for f in group['rawTransitionFrames']] == [b['f'] for a,b in revision_changes], 'Grouped discoveries lost or duplicated a raw transition'
        assert sum(x['newCells'] for x in fog_transitions) == sum(x['newCells'] for x in fog_groups) == cells_changed
        assert sum(x['newRevision']-x['oldRevision'] for x in fog_groups) == revisions_changed
        if sample['region']=='L1-native-fog':
            assert fog_groups and cells_changed > 0, 'Native fog fixture did not exercise discovery'
        else:
            assert sample['region']=='L1-all-explored' and not fog_groups and cells_changed==0
        if not sample['mapOn']:assert len(commits)==0
        assert len(commits)==sample['mapChanges']==trace[-1]['mapCommits']-beginning['mapCommits']
        coverage={'hardwareObservations':frames,'rawRevisionTransitions':len(revision_changes),'discoveryGroups':len(fog_groups),
                  'revisionDelta':revisions_changed,'revealedCellsDelta':cells_changed,'groupedNewCells':sum(x['newCells'] for x in fog_groups),
                  'allRawTransitionsIncludedExactlyOnce':True,'reportEndpointsMatchTrace':True,'logicalMovesMatchTrace':True}
        output['samples'].append({'name':sample['name'],'fps':sample['fps'],
            'minRolling60HardwareFramesFps':sample['minRolling60HardwareFramesFps'],
            'logicalMoves':sample['logicalMoves'],'mapOverruns':sample['mapOverruns'],
            'traceSha256':hashlib.sha256(trace_path.read_bytes()).hexdigest(),
            'coverage':coverage,'beginningState':beginning,'metrics':metrics,'fogTransitions':fog_transitions,
            'discoveryEvents':events,'mapCommits':commits,'anchorChanges':anchor_changes,'diagnostic':diagnostic,
            'pcSamples':sample['pcSamples']})
    return output

result = analyze(args.folder)
partial = [s for s in result['samples'] if s['discoveryEvents']]
assert len(partial)==2
assert [(e['hero'],e['newCells']) for e in partial[0]['discoveryEvents']] == [(e['hero'],e['newCells']) for e in partial[1]['discoveryEvents']], 'OFF/ON fixtures revealed different cells'
result['checks']={'everyHardwareObservationPresent':True,'everyRawDiscoveryIncludedExactlyOnce':True,
                  'revisionAndRevealedCellTotalsAgree':True,'mapOffOnDiscoverySequenceMatches':True,
                  'summaryBoundToReportTracesToolAndFinalRom':True}
(args.folder/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'result':result['result'],'romSha256':result['romSha256'],'samples':[
    {'name':s['name'],'fps':round(s['fps'],3),'events':len(s['discoveryEvents']),
     'renderP99Ms':s['metrics']['render']['p99Ms'],'worldMaxMs':s['metrics']['worldBg0Scroll']['maxMs'],
     'videoMaxMs':s['metrics']['video']['maxMs']} for s in result['samples']]}))
